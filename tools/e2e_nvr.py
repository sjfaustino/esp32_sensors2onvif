"""What the NVR sees: logins, subscriptions, motion events, the live page, past events, and restarts."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
import e2e_common as common
from e2e_files import file_manager_tests
from e2e_sensors import *  # noqa: F401,F403  (the sensor tests it runs on the way)


def test_onvif_rtsp_login():
    # ONVIF and RTSP login.
    control('auth-on')
    check('ONVIF needs a login', soap('GetProfiles')[0] == 400 and 'NotAuthorized' in soap('GetProfiles')[1])
    check('the clock is open before login', soap('GetSystemDateAndTime', '/onvif/device_service')[0] == 200)
    check('ONVIF accepts a digest login', soap_login('GetProfiles', 'onvif-secret')[0] == 200)
    check('ONVIF accepts a plain login', soap_login('GetProfiles', 'onvif-secret', digest=False)[0] == 200)
    check('ONVIF refuses a wrong password', soap_login('GetProfiles', 'wrong')[0] == 400)
    check('ONVIF refuses a wrong user', soap_login('GetProfiles', 'onvif-secret', user='bob')[0] == 400)
    sock, code, challenge = rtsp_describe()
    sock.close()
    check('RTSP asks for a login', code == 401 and any('Digest' in l for l in challenge), (code, challenge))
    sock, code, challenge = rtsp_describe()
    nonce = re.search(r'nonce="([^"]+)"', challenge[0]).group(1)
    uri = f'rtsp://{HOST}:{RTSP}/stream'
    md5 = lambda v: hashlib.md5(v.encode()).hexdigest()
    response = md5(md5('admin:SensorCam:onvif-secret') + ':' + nonce + ':' + md5('DESCRIBE:' + uri))
    sock.sendall(f'DESCRIBE {uri} RTSP/1.0\r\nCSeq: 2\r\nAuthorization: Digest username="admin", realm="SensorCam", nonce="{nonce}", uri="{uri}", response="{response}"\r\n\r\n'.encode())
    check('RTSP accepts a digest login', sock.recv(4096).startswith(b'RTSP/1.0 200'))
    sock.close()
    basic = 'Basic ' + base64.b64encode(b'admin:onvif-secret').decode()
    sock, code, _ = rtsp_describe(basic)
    sock.close()
    check('RTSP accepts a basic login', code == 200, code)
    sock, code, _ = rtsp_describe('Basic ' + base64.b64encode(b'admin:wrong').decode())
    sock.close()
    check('RTSP refuses a wrong password', code == 401, code)
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('GET', '/onvif/snapshot')
    r = c.getresponse()
    r.read()
    c.close()
    check('snapshot needs its token', r.status == 403, r.status)
    uri = re.search(r'<tt:Uri>http://[^/]+([^<]+)</tt:Uri>', soap_login('GetSnapshotUri', 'onvif-secret')[1]).group(1)
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('GET', uri)
    r = c.getresponse()
    r.read()
    c.close()
    check('the snapshot address from a login works', r.status == 200 and 'token=' in uri, (r.status, uri))
    control('auth-off')
    check('ONVIF is open again without a password', soap('GetProfiles')[0] == 200)
    return c, code


def test_start_with_an_nvr_listening(files):
    # An NVR is listening before any alert, as in use. Without one, the hub would keep every alert for a replay.
    puller = PullingNvr()
    codes = [api('GET', f'/api/file?path={p}')[0] for p in ('clear.h264', 'text.json')]
    if any(c not in (200, 404) for c in codes):
        sys.exit(f'stopping: cannot tell whether the board has its own files (HTTP {codes})')
    hub_files_free = codes == [404, 404]
    if not hub_files_free:
        print('SKIP the board has its own clear.h264 or text.json, so checks that replace them are skipped', flush=True)
    file_manager_tests(files, hub_files_free)
    return hub_files_free, puller


def test_nvr_first_connection_order():
    # The order the NVR used when first connecting.
    device, media = '/onvif/device_service', '/onvif/media'
    for action, path in [('GetSystemDateAndTime', device), ('GetProfiles', media), ('GetStreamUri', media), ('GetCapabilities', device),
                         ('GetDeviceInformation', device), ('SetSynchronizationPoint', media), ('GetAudioSources', media),
                         ('GetVideoEncoderConfiguration', media), ('GetVideoEncoderConfigurationOptions', media)]:
        code, data = soap(action, path)
        check(f'{action} answered', code == 200 and f'{action}Response' in data, data[:200])
    code, data = soap('GetStreamUri')
    check('stream URI follows the Host header', f'rtsp://{HOST}:{RTSP}/stream' in data, data[:300])
    check('unknown call is refused', soap('NoSuchCall')[0] == 400)
    return code, data, device, path


def test_nvr_subscribe(device, files, hub_files_free):
    # The NVR sends Subscribe to device_service, twice, with one listener URL.
    nvr = FakeNvrListener()
    inner = f'<ConsumerReference><Address>http://{LOCAL}:{nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
    subscribers = status()['subscribers']
    for _ in range(2):
        code, data = soap('Subscribe', device, inner)
        check('Subscribe accepted on device_service', code == 200 and 'SubscribeResponse' in data, data[:200])
    check('two identical Subscribes make one subscriber', status()['subscribers'] == subscribers + 1)
    wait_until(lambda: len(nvr.events) >= 2)
    check('initial state is pushed as false', nvr.states()[:1] == ['false'], nvr.states())
    check('initial push is marked Initialized', 'PropertyOperation="Initialized"' in nvr.events[0][1])
    check('each Subscribe gets its initial state', len(nvr.events) == 2, len(nvr.events))

    if hub_files_free:
        colour = grab_colour()
        check('picture is green when clear', colour == 'green', colour)

        # A replacement "all clear" takes effect at once, and deleting it restores the built-in one.
        colour = test_replacement_all_clear(files)
    return code, colour, data, nvr


def test_sensor_alert_makes_a_motion_event(nvr):
    # A sensor alert makes a motion event, and the picture shows it. Sensor 3 has no files.
    check('unknown sensor is rejected', get('/sensor?mac=02-00-00-00-00-09')[0] == 404)
    before = len(nvr.events)
    started = time.time()
    code, data = get(f'/sensor?{sid(3)}')
    check('sensor alert accepted', code == 200 and '"ok":true' in data, data)
    wait_until(lambda: len(nvr.states()) > before, timeout=1)
    check('alert reaches the NVR quickly', nvr.states()[before:before + 1] == ['true'], nvr.states())
    check('one notification per event despite two Subscribes', len(nvr.events) == before + 1, len(nvr.events) - before)
    pushed = nvr.events[before][1] if len(nvr.events) > before else ''
    check('push has both motion topics', 'VideoSource/MotionAlarm' in pushed and 'CellMotionDetector/Motion' in pushed)
    colour = grab_colour()
    check('built-in alert picture is shown for a sensor with no files', colour == 'red', colour)
    return before, code, colour, data, started


def test_alert_repeats_then_ends(before, hub_files_free, nvr, started):
    # Repeated "true" while active, then "false" when the hold ends.
    time.sleep(max(HOLD_MS / 1000 + 1.5 - (time.time() - started), 0))
    during = nvr.events[before:]
    states = [re.search(r'Name="State" Value="(\w+)"', e[1]).group(1) for e in during]
    check('true repeats while active', states.count('true') >= 6, states)
    trues = [e[0] for e, s in zip(during, states) if s == 'true']
    gaps = [b - a for a, b in zip(trues, trues[1:])]
    check('repeat gap stays under the 2 s latch', bool(gaps) and max(gaps) < 1.9, gaps)
    check('false is sent when the hold ends', states[-1] == 'false', states)
    if hub_files_free:
        colour = grab_colour()
        check('picture is green again', colour == 'green', colour)
    return colour


def test_clear_report_does_not_end(nvr):
    # A clear report does not end the alert early, so activations close together make one NVR record.
    before = len(nvr.events)
    started = time.time()
    get(f'/sensor?{sid(2)}')
    time.sleep(0.5)  # the clear comes a moment after the activation, as from a real sensor
    check('a clear report is accepted', get(f'/sensor?{sid(2)}&state=clear')[0] == 200)
    time.sleep(1)  # time for a clear report to wrongly end the alert
    check('a clear report does not end the alert', name(2) in status()['active'] and 'false' not in nvr.states()[before:], nvr.states()[before:])
    get(f'/sensor?{sid(2)}')
    history = poll(event_list, lambda h: [e['active'] for e in h[:3]] == [True, False, True])
    check('each activation is in the event history', [e['active'] for e in history[:3]] == [True, False, True], history[:3])
    time.sleep(max(HOLD_MS / 1000 * 0.8 - (time.time() - started), 0))
    check('a second activation within the hold extends the same alert', name(2) in status()['active'] and 'false' not in nvr.states()[before:], nvr.states()[before:])
    return before, started


def test_live_page():
    # The Live page shows the picture being sent. The picture changes on the next frame after the alert ends.
    time.sleep(1.5)
    idle = get_json('/api/live')
    code, idle_frame = api('GET', '/api/live/frame')
    check('Live sends a whole picture', code == 200 and idle_frame.startswith(b'\0\0\0\1') and idle_frame[4] & 0x1F == 7, code)
    get(f'/sensor?{sid(2)}')
    firing = poll(live, lambda j: j['seq'] != idle['seq'])
    check('Live reports a new picture when a sensor fires', firing['seq'] != idle['seq'], firing)
    check('Live picture changes when a sensor fires', api('GET', '/api/live/frame')[1] != idle_frame)
    clear(2)
    cleared = poll(live, lambda j: j['seq'] != firing['seq'])
    check('Live reports the picture going back', cleared['seq'] != firing['seq'], cleared)
    check('Live picture goes back', api('GET', '/api/live/frame')[1] == idle_frame)
    check('Live needs the web password', not PASSWORD or api('GET', '/api/live', auth=False)[0] == 401)
    return code, firing, idle


def test_esp_now_page():
    # The ESP-NOW page lists every sensor node.
    node_list = get_json('/api/espnow')
    entry = [n for n in node_list['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
    check('ESP-NOW page lists a sensor that has reported', len(entry) == 1 and entry[0]['state'] == 'idle' and entry[0]['ago'] is not None, node_list)
    quiet = [n for n in node_list['nodes'] if n['node'] == SENSORS[6][0] and n['input'] == SENSORS[6][1]]
    check('ESP-NOW page shows the state of each sensor', len(quiet) == 1 and quiet[0]['state'] in ('never', 'idle'), node_list)
    get(f'/sensor?{sid(2)}')
    wait_until(lambda: name(2) in status()['active'])
    firing = [n for n in get_json('/api/espnow')['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
    check('ESP-NOW page shows an active sensor', firing and firing[0]['state'] == 'active', firing)
    clear(2)
    check('ESP-NOW page needs the web password', not PASSWORD or api('GET', '/api/espnow', auth=False)[0] == 401)

    if not DEVICE:  # the PC build stands in for the radio
        def radio():
            return get_json('/api/espnow')

        def node_entry(d):
            return [n for n in d['nodes'] if n['node'] == SENSORS[1][0] and n['input'] == SENSORS[1][1]][0]

        control('radio-known')
        seen = node_entry(radio())
        check('radio report shows signal, battery and report number', (seen['rssi'], seen['battery'], seen['counter']) == (-61, 55, 77), seen)
        to, _, sent = control('last-sent').partition(' ')
        expect = bytes([3, 1, 0]) + (77).to_bytes(4, 'little')
        got = bytes.fromhex(sent)
        signature = hmac.new(b'test-espnow-key-0123456789', bytes.fromhex(to) + got[:26], hashlib.sha1).digest()[:8]
        check('the hub confirms a report to its sensor, signed for that sensor', to == '020000000001' and got[:7] == expect and got[26:] == signature, (to, sent))
        clear(1)
        check('a report without a battery keeps the last reading', node_entry(radio())['battery'] == 55, node_entry(radio()))
        check('no unknown senders yet', radio()['unknown'] == [], radio()['unknown'])
        control('radio-huge')
        check('a report number above 2 billion stays positive', node_entry(radio())['counter'] == 2147483700, node_entry(radio()))
        control('last-sent')
        control('radio-huge')
        check('a resend is confirmed again, because its sensor missed the first confirmation', control('last-sent') != '')
        clear(1)
        control('radio-unknown')
        check('an unknown sender gets no confirmation', control('last-sent') == '')
        control('radio-unknown')
        stranger = radio()['unknown']
        check('unknown sender is listed', len(stranger) == 1 and stranger[0]['mac'] == '02-00-00-00-00-99' and stranger[0]['input'] == 1 and stranger[0]['rssi'] == -61, stranger)
        check('unknown sender messages are counted', stranger and stranger[0]['messages'] == 2, stranger)
        check('unknown sender is not listed as a node', not [n for n in radio()['nodes'] if n['node'] == '02-00-00-00-00-99'])
        api('POST', '/api/mkdir?path=02-00-00-00-00-99/1')
        check('adding the sender removes it from the unknown list', radio()['unknown'] == [], radio()['unknown'])
        api('DELETE', '/api/file?path=02-00-00-00-00-99')

        # Signed messages.
        before, events, history = test_signed_messages(node_entry, radio)

        # Weak signal.
        test_weak_signal(node_entry, radio)

        # Sensor names.
        label = 'Front "gate"'.encode()
        check('a sensor can be named', api('PUT', f'/api/file?path={name(1)}/name.txt', label)[0] == 200)
        check('the name is shown with the node', node_entry(radio())['name'] == label.decode(), node_entry(radio()))
        check('the name is on the Sensors page data', any(n.get('name') == label.decode() for s in get_json('/api/tree')['nodes'] for n in s['sensors']))

        # Battery and silence alerts.
        test_battery_silence_alerts(label, node_entry, radio)

        # Per-sensor settings.
        code, path = test_per_sensor_settings()

        # The full event history.
        code = test_full_event_history(path)

        # Webhook.
        code, received = test_webhook(path)

        # No memory left at all: a radio alert still starts, is logged and is confirmed, and the hub keeps running.
        test_no_memory_left_at_all()

        # The Events page asks every 3 seconds; an unchanged list is answered without being built again.
        code = test_events_list_tag()

        # Alerts the NVR did not get. Nothing subscribes from 192.0.2.8, so with that as the NVR's address it is not listening.
        gone = test_alerts_the_nvr_missed(path)
        # The NVR goes again during the replay: the replay waits again, without a second entry or message.
        test_nvr_goes_again_during_replay()

        # A report that arrives while every web connection is busy waits for one, instead of being turned away.
        idle, s = test_report_waits_for_busy_connections()

        # A live alert never shares its NVR record with a past event: the past event stops and plays again later.
        test_live_alert_interrupts_past_event()

        # Being kept off the network is logged when the hub is back.
        control('network-lost')
        lost = event_list()[0]
        check('time off the network is logged', lost['kind'] == 9 and lost['sensor'] == 'hub' and lost['off_s'] == 125 and lost['event'] == 'network lost', lost)

        # Other names the hub answers to.
        check('other names for the hub can be saved', api('POST', '/api/settings', 'hosts=' + urllib.parse.quote('esp-alarmhub.fritz.box, hub.example.lan'))[0] == 200)
        check('a bad other name is refused', api('POST', '/api/settings', 'hosts=' + urllib.parse.quote('bad name!'))[0] == 400)
        check('more than four other names are refused', api('POST', '/api/settings', 'hosts=a,b,c,d,e')[0] == 400)

        # Firmware and backup.
        check('firmware update needs the web password', api('PUT', '/api/firmware', b'\xe9' * 100, auth=False)[0] == 401)
        check('valid firmware is accepted', api('PUT', '/api/firmware', b'\xe9' + b'\x01' * 9000)[0] == 200)
        check('other files are refused as firmware', api('PUT', '/api/firmware', b'not firmware' * 500)[0] == 400)
        # During an upload the hub drops video and takes nothing new, so the update has the memory it needs.
        code, reply = test_firmware_upload_pauses_the_hub()

        # ONVIF and RTSP login.
        c, code = test_onvif_rtsp_login()
    return before, c, code, events, gone, idle, quiet, received, reply, s, seen


def test_poll_timeout_waits_for_news(address):
    # A poll with a timeout waits for news, instead of being answered at once.
    started = time.time()
    quiet = soap('PullMessages', address, '<Timeout>PT2S</Timeout>')
    check('a quiet poll waits for its timeout', 1.5 < time.time() - started < 4 and 'NotificationMessage' not in quiet[1], time.time() - started)
    held = {}
    def hold():
        begun = time.time()
        held['reply'] = soap('PullMessages', address, '<Timeout>PT8S</Timeout>')
        held['took'] = time.time() - begun
    waiter = threading.Thread(target=hold)
    waiter.start()
    time.sleep(0.5)  # so the poll is already being held when the alert comes
    get(f'/sensor?{sid(3)}')
    waiter.join(9)
    check('a held poll is answered when something changes', held.get('took', 99) < 3 and 'Value="true"' in held['reply'][1], held.get('took'))
    clear(3)
    soap('PullMessages', address)


def test_subscription_is_its_makers(address):
    # Another device cannot pull from or end the NVR's subscription: pulling it would take the change the NVR waits for.
    if DEVICE:  # needs a second address on this computer
        return
    get(f'/sensor?{sid(3)}')
    other = soap('PullMessages', address, source='127.0.0.2')
    check("another address pulling the NVR's subscription learns nothing", 'NotificationMessage' not in other[1], other)
    check("another address cannot end the NVR's subscription", soap('Unsubscribe', address, source='127.0.0.2')[0] == 400)
    own = soap('PullMessages', address)
    check('the NVR still gets the change', 'Value="true"' in own[1], own)
    clear(3)
    soap('PullMessages', address)


def test_ws_discovery():
    # WS-Discovery, sent directly.
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(3)
    probe = ('<e:Envelope xmlns:e="http://www.w3.org/2003/05/soap-envelope" xmlns:w="a" xmlns:d="b" xmlns:dn="c"><e:Header><w:MessageID>uuid:test-1</w:MessageID>'
             '</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body></e:Envelope>')
    udp.sendto(probe.encode(), (HOST, DISCOVERY))
    reply = udp.recv(4096).decode()
    check('discovery answers a Probe', 'ProbeMatch' in reply and 'uuid:test-1' in reply and f'http://{HOST}:{ONVIF}/onvif/device_service' in reply, reply[:300])


def test_slightly_old_report_still_counts():
    # A slightly old report still counts as live.
    get(f'/sensor?{sid(6)}&age=5')
    now = status()
    check('recent report is treated as live', now['active'] == [name(6)], now)
    clear(6)
    check('live report is not marked delayed', json.loads(get('/events')[1])[1]['delayed'] is False)
    return now


def test_cached_alerts(nvr):
    # Cached alerts: amber picture, one record each, true time kept in the event list.
    before = len(nvr.events)
    get(f'/sensor?{sid(4)}&age=100&direction=1&distance=250&battery=80')
    get(f'/sensor?{sid(5)}&age=unknown')
    now = poll(status, lambda st: st['active'] == [name(4)] and st['delayed_waiting'] == 1)
    check('first cached alert plays, second waits', now['active'] == [name(4)] and now['delayed_waiting'] == 1, now)
    check('delayed alert still reaches the NVR', 'true' in nvr.states()[before:], nvr.states()[before:])
    colour = grab_colour()
    check('missing direction picture falls back to the delayed one', colour == 'amber', colour)
    wait_until(lambda: status()['active'] == [name(5)], timeout=20)
    check('second cached alert plays after the first', status()['active'] == [name(5)])
    wait_until(lambda: 'false' in nvr.states()[before:] and 'true' in nvr.states()[before:][nvr.states()[before:].index('false'):], timeout=3)
    seen = nvr.states()[before:]
    first_false = seen.index('false') if 'false' in seen else -1
    check('NVR sees the first event end before the second starts', first_false >= 0 and 'true' in seen[first_false:], seen)

    events = json.loads(get('/events')[1])
    by_sensor = {e['sensor']: e for e in reversed(events)}
    e4, e5 = by_sensor[name(4)], by_sensor[name(5)]
    check('event list keeps distance, direction and battery', e4['targets'] == [{'distance_cm': 250, 'direction': 1}] and e4['battery'] == 80, e4)
    if e4['received']:
        received = datetime.datetime.strptime(e4['received'], '%Y-%m-%dT%H:%M:%SZ')
        happened = datetime.datetime.strptime(e4['happened'], '%Y-%m-%dT%H:%M:%SZ')
        check('event time is arrival minus age', (received - happened).total_seconds() == 100, e4)
    check('unknown age has no event time', e5['happened'] is None and e5['age_s'] is None and e5['delayed'] is True, e5)
    heard = {h['sensor']: h for h in status()['heard']}
    check('status tracks last heard and battery', heard[name(4)]['battery'] == 80 and heard[name(4)]['silent'] is False, heard)
    get(f'/sensor?{sid(4)}&state=clear&battery=300')
    heard = {h['sensor']: h for h in status()['heard']}
    check('an impossible battery reading does not replace the last good one', heard[name(4)]['battery'] == 80, heard)
    wait_until(lambda: not status()['active'], timeout=15)
    return before, colour


def test_node_past_event_time(s):
    # Each node sets how old an alert must be to count as a past event. Sensor 3 has no pictures.
    node3 = SENSORS[3][0]
    check('a node can have its own past-event time', api('POST', f'/api/node-settings?path={node3}', 'late=300')[0] == 200)
    check('the Sensors page data shows it', [s for s in get_json('/api/tree')['nodes'] if s['mac'] == node3][0]['late'] == 300)
    check("it is kept in the node's folder", api('GET', f'/api/file?path={node3}/settings.txt') == (200, b'late=300\n'))
    get(f'/sensor?{sid(3)}&age=100')
    wait_until(lambda: status()['active'] == [name(3)])
    check("an alert younger than the node\'s time is live", status()['active'] == [name(3)] and json.loads(get('/events')[1])[0]['delayed'] is False, json.loads(get('/events')[1])[0])
    colour = grab_colour()
    check('a live alert with no pictures shows the built-in alert', colour == 'red', colour)
    clear(3)
    get(f'/sensor?{sid(3)}&age=400')
    wait_until(lambda: status()['active'] == [name(3)])
    check("an alert older than the node\'s time is a past event", json.loads(get('/events')[1])[0]['delayed'] is True and status()['active'] == [name(3)], json.loads(get('/events')[1])[0])
    colour = grab_colour()
    check('a past event with no pictures shows the built-in past-event picture', colour == 'amber', colour)
    clear(3)
    check('a bad past-event time is refused', api('POST', f'/api/node-settings?path={node3}', 'late=0')[0] == 400)
    check('a node can have its own silent time', api('POST', f'/api/node-settings?path={node3}', 'late=300&silent=600')[0] == 200
          and api('GET', f'/api/file?path={node3}/settings.txt') == (200, b'late=300\nsilent=600\n'))
    check('the Sensors page data shows it', [s for s in get_json('/api/tree')['nodes'] if s['mac'] == node3][0]['silent'] == 600)
    check('a silent time under a minute is refused', api('POST', f'/api/node-settings?path={node3}', 'silent=30')[0] == 400)
    check('settings for a node that does not exist are refused', api('POST', '/api/node-settings?path=02-00-00-00-00-77', 'late=60')[0] == 404)
    check('clearing the node settings removes the file', api('POST', f'/api/node-settings?path={node3}', '')[0] == 200 and api('GET', f'/api/file?path={node3}/settings.txt')[0] == 404)
    get(f'/sensor?{sid(3)}&age=100')
    wait_until(lambda: event_list()[0]['active'])
    check('without the setting, 100 seconds is a past event again', json.loads(get('/events')[1])[0]['delayed'] is True)
    clear(3)


def test_hung_listener(device, nvr):
    # A listener that accepts and never answers must not stall the camera or the good subscriber.
    hung = socket.socket()
    hung.bind((LOCAL, 0))
    hung.listen(8)
    soap('Subscribe', device, f'<ConsumerReference><Address>http://{LOCAL}:{hung.getsockname()[1]}/x</Address></ConsumerReference>')
    before = len(nvr.events)
    get(f'/sensor?{sid(1)}')
    slowest = 0
    for _ in range(15):
        t = time.time()
        code = soap('GetSystemDateAndTime', device)[0]
        slowest = max(slowest, time.time() - t)
        time.sleep(0.1)
    check('camera keeps answering with a hung listener', code == 200 and slowest < 0.5, slowest)
    check('good subscriber still gets events with a hung one present', 'true' in nvr.states()[before:], nvr.states()[before:])
    clear(1)
    check('still answering at the end', soap('GetSystemDateAndTime', device)[0] == 200)


def test_pushes_wait_for_sockets(device, nvr, proc):
    # Web connections and pushes share a few sockets, so a push may have to wait for one. It must still go out.
    if proc:
        wait_until(lambda: not status()['active'], timeout=15)
        time.sleep(3)  # the hung listener's socket is given up
        second = FakeNvrListener()
        soap('Subscribe', device, f'<ConsumerReference><Address>http://{LOCAL}:{second.port}/x</Address></ConsumerReference>')
        first_seen, second_seen = len(nvr.events), len(second.events)
        idle = [socket.create_connection((HOST, WEB), timeout=5) for _ in range(5)]
        time.sleep(0.2)  # the hub accepts the idle connections, so they hold sockets
        get(f'/sensor?{sid(1)}')
        time.sleep(2)  # the push must get out while the sockets stay taken
        for c in idle:
            c.close()
        check('pushes that wait for a socket still go out', len(nvr.events) > first_seen and len(second.events) > second_seen, (len(nvr.events), first_seen, len(second.events), second_seen))
        clear(1)


def test_sensor_limit():
    # The limit of 32 is refused up front. Everything made here is under a node this test owns, and is removed again.
    extra = '02-00-00-00-33-33'
    free = 32 - status()['sensors']
    made = [api('POST', f'/api/mkdir?path={extra}/{i + 1}')[0] for i in range(free)]
    check('sensor folders up to the limit are accepted', set(made) <= {200}, made)
    check('a sensor folder beyond the limit is refused', api('POST', f'/api/mkdir?path={extra}/{free + 1}')[0] == 409)
    check('a name for a sensor beyond the limit is refused', api('PUT', f'/api/file?path={extra}/{free + 1}/name.txt', b'x')[0] == 409)
    if free:
        check('an existing sensor can still be changed at the limit', api('PUT', f'/api/file?path={extra}/1/name.txt', b'x')[0] == 200)
    api('DELETE', f'/api/file?path={extra}')
    check('the limit frees up when sensors are deleted', status()['sensors'] == 32 - free, status())


def test_deleted_sensor_delayed_alert(gone, newcomer):
    # Past events from earlier checks play out first, so none is playing: a live alert would send it back to the queue.
    wait_until(lambda: status()['delayed_waiting'] == 0 and not status()['active'], timeout=60)
    get(f'/sensor?{sid(1)}')
    get(f'/sensor?mac={gone}&input=1&age=unknown')
    check('a delayed alert waits while another sensor is active', status()['delayed_waiting'] == 1, status())
    api('DELETE', f'/api/file?path={gone}')
    api('POST', f'/api/mkdir?path={newcomer}/1')
    clear(1)
    shown = set()

    def watched():
        st = status()
        shown.update(st['active'])
        return st
    return shown, watched


def test_deleted_sensor_delayed_alert_not_played(newcomer, shown, watched):
    # Until the queue is empty, which may first wait out an earlier past event's gap, then through the time it
    # would have played for, since it must not appear.
    poll(watched, lambda st: st['delayed_waiting'] == 0, timeout=2 * (HOLD_MS / 1000 + 4) + 5, step=0.2)
    end = time.time() + HOLD_MS / 1000 + 1
    while time.time() < end:
        watched()
        time.sleep(0.2)
    check("a deleted sensor\'s delayed alert does not play as another sensor", f'{newcomer}/1' not in shown and status()['delayed_waiting'] == 0, shown)
    api('DELETE', f'/api/file?path={newcomer}')
    return end


def test_late_alerts_from_one_sensor():
    # Late alerts from one sensor close together are one record, as they would have been live.
    get(f'/sensor?{sid(1)}')
    get(f'/sensor?{sid(5)}&age=200')
    get(f'/sensor?{sid(5)}&age=197')
    check('late alerts from one sensor within its hold make one record', status()['delayed_waiting'] == 1, status())
    get(f'/sensor?{sid(5)}&age=120')
    check('a late alert further apart makes its own record', status()['delayed_waiting'] == 2, status())
    clear(1)
    end = time.time() + 2 * (HOLD_MS / 1000 + 4) + 5
    wait_until(lambda: not status()['delayed_waiting'], timeout=end - time.time())
    check('both late records play', status()['delayed_waiting'] == 0, status())
    check('the page is served with a query string', get('/?x=1')[0] == 200)


def test_deleting_folders_and_restart(files, proc, puller, storage):
    # Deleting folders removes the sensors, without a reboot.
    count = status()['sensors']
    check('sensor folder can be deleted', api('DELETE', f'/api/file?path={name(6)}')[0] == 200)
    check('deleted sensor is unknown at once', get(f'/sensor?{sid(6)}')[0] == 404 and status()['sensors'] == count - 1)
    check('node folder can be deleted with its contents', api('DELETE', f'/api/file?path={A}')[0] == 200)
    check('all its sensors are gone', get(f'/sensor?{sid(1)}')[0] == 404 and status()['sensors'] == count - 4)

    if proc:
        # A planned restart writes the waiting alerts as they are, so an alert the NVR already got does not play again.
        wait_until(lambda: not status()['active'] and status()['delayed_waiting'] == 0, timeout=60)  # what earlier checks left plays out
        get(f'/sensor?{sid(4)}')
        wait_until(lambda: name(4) not in status()['active'], timeout=20)  # its hold runs out, well after its delivery check at 5 s
        control('save-now')
        proc.kill()
        proc.wait()
        proc = start_hub(storage, PASSWORD, '1500')
        common.HUB.stdout.readline()
        puller.stop()
        puller = PullingNvr()
        time.sleep(3)  # time for a stale entry to wrongly start playing
        check('after a planned restart, an alert the NVR got does not play again', name(4) not in status()['active'] and status()['delayed_waiting'] == 0, status())

        # A past event still waiting when the power goes must play after the restart: its sensor was told it was received.
        get(f'/sensor?{sid(4)}')  # keeps it waiting
        get(f'/sensor?{sid(5)}&age=500')
        check('a past event waits while another alert is on', status()['delayed_waiting'] >= 1, status())
        proc.kill()
        proc.wait()
        proc = start_hub(storage, PASSWORD, '1500')
        common.HUB.stdout.readline()
        puller.stop()  # its subscription was in the hub that stopped
        puller = PullingNvr()
        check('folders are still sensors after a restart', status()['sensors'] == count - 4, status())
        check('stored pictures survive a restart', api('GET', f'/api/file?path={name(4)}/active.h264')[1] == files['active.h264'])
        check('a past event waiting at the restart still plays', wait_until(lambda: name(5) in status()['active']), status())
        control('end-alerts')
        quiet_nodes = poll(lambda: get_json('/api/espnow')['nodes'], lambda n: n and all(x['state'] == 'silent' for x in n))
        check('a node quiet since the restart is reported silent', quiet_nodes and all(n['state'] == 'silent' for n in quiet_nodes), quiet_nodes)
        check('going silent after a restart is logged', any(e['kind'] == 1 for e in get_json('/api/events')))
        starts = [e for e in event_list() if e['kind'] == 8]
        check('the start is logged with its reason and how long the hub was off', starts and starts[0]['sensor'] == 'hub' and starts[0]['detail'] == 'PC build started'
              and starts[0]['event'] == 'hub started' and starts[0]['off_s'] is not None and starts[0]['off_s'] < 3600, starts[:2])
        # A third start soon after the last two, none of them asked for: the hub counts them as restarting often.
        proc.kill()
        proc.wait()
        proc = start_hub(storage, PASSWORD, '1500')
        common.HUB.stdout.readline()
        loop = poll(event_list, lambda l: any(e['kind'] == 16 for e in l))
        loop = [e for e in loop if e['kind'] == 16]
        check('three unplanned starts within an hour are logged as restarting often', loop and loop[0]['sensor'] == 'hub' and loop[0]['detail'] == '3 times in an hour', loop)
    api('DELETE', f'/api/file?path={B}')
    return proc
