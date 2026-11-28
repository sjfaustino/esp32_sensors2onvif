"""What the NVR sees: logins, subscriptions, motion events, the live page, past events, and restarts."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
from e2e_sensors import node_entry, radio

DEVICE_SERVICE = '/onvif/device_service'


@case()
def nvr_first_connection_order(s):
    """The calls, in the order the real NVR made them when it first connected."""
    media = '/onvif/media'
    for action, path in [('GetSystemDateAndTime', DEVICE_SERVICE), ('GetProfiles', media), ('GetStreamUri', media), ('GetCapabilities', DEVICE_SERVICE),
                         ('GetDeviceInformation', DEVICE_SERVICE), ('SetSynchronizationPoint', media), ('GetAudioSources', media),
                         ('GetVideoEncoderConfiguration', media), ('GetVideoEncoderConfigurationOptions', media)]:
        code, data = soap(action, path)
        check(f'{action} answered', code == 200 and f'{action}Response' in data, data[:200])
    code, data = soap('GetStreamUri')
    check('stream URI follows the Host header', f'rtsp://{HOST}:{RTSP}/stream' in data, data[:300])
    check('unknown call is refused', soap('NoSuchCall')[0] == 400)


def unsubscribe(reply):
    """Ends the push subscription a SubscribeResponse made, if it says where it is."""
    found = re.search(r'/onvif/sub/\d+', reply)
    if found:
        soap('Unsubscribe', found.group(0))


@case()
def nvr_subscribe(s):
    """The NVR sends Subscribe to device_service, twice, with one listener address: one subscriber, told the state at once,
    and told of each event once."""
    nvr = FakeNvrListener()
    inner = f'<ConsumerReference><Address>http://{LOCAL}:{nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
    subscribers = status()['subscribers']
    replies = []
    for _ in range(2):
        code, data = soap('Subscribe', DEVICE_SERVICE, inner)
        replies.append(data)
        check('Subscribe accepted on device_service', code == 200 and 'SubscribeResponse' in data, data[:200])
    try:
        check('two identical Subscribes make one subscriber', status()['subscribers'] == subscribers + 1)
        wait_until(lambda: len(nvr.events) >= 2)
        check('initial state is pushed as false', nvr.states()[:1] == ['false'], nvr.states())
        check('initial push is marked Initialized', 'PropertyOperation="Initialized"' in nvr.events[0][1])
        check('each Subscribe gets its initial state', len(nvr.events) == 2, len(nvr.events))
        before = len(nvr.events)
        get(f'/sensor?{sid(3)}')
        wait_until(lambda: len(nvr.events) > before, timeout=2)
        time.sleep(0.3)  # time for a second notification to be wrongly sent
        check('one notification per event despite two Subscribes', len(nvr.events) == before + 1, len(nvr.events) - before)
    finally:
        unsubscribe(replies[-1])


@case()
def an_alert_makes_a_motion_event(s):
    """A sensor alert reaches the NVR as a motion event, repeated while it lasts and ended when its hold runs out; the
    picture shows it. Sensor 3 has no pictures."""
    nvr = s.nvr
    check('unknown sensor is rejected', get('/sensor?mac=02-00-00-00-00-09')[0] == 404)
    before = len(nvr.events)
    started = time.time()
    code, data = get(f'/sensor?{sid(3)}')
    check('sensor alert accepted', code == 200 and '"ok":true' in data, data)
    wait_until(lambda: len(nvr.states()) > before, timeout=1)
    check('alert reaches the NVR quickly', nvr.states()[before:before + 1] == ['true'], nvr.states())
    pushed = nvr.events[before][1] if len(nvr.events) > before else ''
    check('push has both motion topics', 'VideoSource/MotionAlarm' in pushed and 'CellMotionDetector/Motion' in pushed)
    colour = grab_colour()
    check('built-in alert picture is shown for a sensor with no files', colour == 'red', colour)
    # Repeated "true" while active, then "false" when the hold ends.
    time.sleep(max(HOLD_MS / 1000 + 1.5 - (time.time() - started), 0))
    during = nvr.events[before:]
    states = [re.search(r'Name="State" Value="(\w+)"', e[1]).group(1) for e in during]
    check('true repeats while active', states.count('true') >= 6, states)
    trues = [e[0] for e, st in zip(during, states) if st == 'true']
    gaps = [b - a for a, b in zip(trues, trues[1:])]
    check('repeat gap stays under the 2 s latch', bool(gaps) and max(gaps) < 1.9, gaps)
    check('false is sent when the hold ends', states[-1:] == ['false'], states)
    if s.hub_files_free:
        colour = grab_colour()
        check('picture is green again', colour == 'green', colour)


@case()
def a_clear_report_does_not_end_the_alert(s):
    """A clear report does not end the alert early, so activations close together make one NVR record."""
    nvr = s.nvr
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
    # Waited for rather than slept: over a slow network the last activation lands late, and its hold ends later.
    wait_until(lambda: 'false' in nvr.states()[before:], timeout=HOLD_MS / 1000 * 0.4 + 6)
    check('the alert ends when the hold after the last activation runs out', nvr.states()[before:][-1:] == ['false'] and nvr.states()[before:].count('false') == 1, nvr.states()[before:])


@case()
def live_page(s):
    """The Live page shows the picture being sent, which changes on the next frame after an alert starts or ends."""
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


@case()
def nodes_page(s):
    """The Nodes and pairing page lists every sensor, with its state."""
    get(f'/sensor?{sid(2)}')
    clear(2)
    node_list = get_json('/api/espnow')
    entry = [n for n in node_list['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
    check('Nodes and pairing page lists a sensor that has reported', len(entry) == 1 and entry[0]['state'] == 'idle' and entry[0]['ago'] is not None, node_list)
    quiet = [n for n in node_list['nodes'] if n['node'] == SENSORS[6][0] and n['input'] == SENSORS[6][1]]
    check('Nodes and pairing page shows the state of each sensor', len(quiet) == 1 and quiet[0]['state'] in ('never', 'idle'), node_list)
    get(f'/sensor?{sid(2)}')
    wait_until(lambda: name(2) in status()['active'])
    firing = [n for n in get_json('/api/espnow')['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
    check('Nodes and pairing page shows an active sensor', firing and firing[0]['state'] == 'active', firing)
    clear(2)
    check('Nodes and pairing page needs the web password', not PASSWORD or api('GET', '/api/espnow', auth=False)[0] == 401)


@case(pc=True)
def radio_reports(s):
    """Reports over the radio, which the PC build stands in for: confirmed to their sensor, and from strangers listed."""
    control('last-sent')
    control('radio-known')
    seen = node_entry(radio())
    to, _, sent = control('last-sent').partition(' ')
    got = bytes.fromhex(sent)
    number = int.from_bytes(got[3:7], 'little')
    check('radio report shows signal, battery and report number', (seen['rssi'], seen['battery'], seen['counter']) == (-61, 55, number) and number > 0, seen)
    signature = hmac.new(b'test-espnow-key-0123456789', bytes.fromhex(to) + got[:26], hashlib.sha1).digest()[:8]
    check('the hub confirms a report to its sensor, signed for that sensor', to == '020000000001' and got[:3] == bytes([3, 1, 0]) and got[26:] == signature, (to, sent))
    clear(1)
    check('a report without a battery keeps the last reading', node_entry(radio())['battery'] == 55, node_entry(radio()))
    control('radio-huge')
    check('a report number above 2 billion stays positive', node_entry(radio())['counter'] == 2147483700, node_entry(radio()))
    control('last-sent')
    control('radio-huge')
    check('a resend is confirmed again, because its sensor missed the first confirmation', control('last-sent') != '')
    clear(1)
    stranger = '02-00-00-00-00-99'
    api('DELETE', f'/api/file?path={stranger}')
    check('no unknown senders yet', [u for u in radio()['unknown'] if u['mac'] == stranger] == [], radio()['unknown'])
    control('radio-unknown')
    check('an unknown sender gets no confirmation', control('last-sent') == '')
    control('radio-unknown')
    listed = [u for u in radio()['unknown'] if u['mac'] == stranger]
    check('unknown sender is listed', len(listed) == 1 and listed[0]['input'] == 1 and listed[0]['rssi'] == -61, listed)
    check('unknown sender messages are counted', listed and listed[0]['messages'] == 2, listed)
    check('unknown sender is not listed as a node', not [n for n in radio()['nodes'] if n['node'] == stranger])
    api('POST', f'/api/mkdir?path={stranger}/1')
    check('adding the sender removes it from the unknown list', [u for u in radio()['unknown'] if u['mac'] == stranger] == [], radio()['unknown'])
    api('DELETE', f'/api/file?path={stranger}')


@case(pc=True)
def sensor_names(s):
    """A name for a sensor, shown wherever it is."""
    label = 'Back "door"'
    try:
        check('a sensor can be named', api('POST', f'/api/sensor-settings?path={name(1)}', 'name=' + urllib.parse.quote(label))[0] == 200)
        check('a name on two lines is refused', api('POST', f'/api/sensor-settings?path={name(1)}', 'name=a%0Ab')[0] == 400)
        check('a name over 64 characters is refused', api('POST', f'/api/sensor-settings?path={name(1)}', 'name=' + 'x' * 65)[0] == 400)
        check('the name is shown with the node', node_entry(radio())['name'] == label, node_entry(radio()))
        check('the name is on the Sensors page data', any(n.get('name') == label for x in get_json('/api/tree')['nodes'] for n in x['sensors']))
    finally:
        api('POST', f'/api/sensor-settings?path={name(1)}', 'name=' + urllib.parse.quote(SENSOR_1_NAME))


@case(pc=True)
def time_off_the_network_is_logged(s):
    """Being kept off the network is logged when the hub is back."""
    control('network-lost')
    lost = event_list()[0]
    check('time off the network is logged', lost['kind'] == 9 and lost['sensor'] == 'hub' and lost['off_s'] == 125 and lost['event'] == 'network lost', lost)


@case(pc=True)
def other_names_for_the_hub(s):
    """Other names the hub answers to, besides its own."""
    def asked_as(host):
        c = http.client.HTTPConnection(HOST, WEB, timeout=5)
        c.request('GET', '/status', headers={'Host': host, 'Authorization': 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()})
        r = c.getresponse()
        r.read()
        c.close()
        return r.status

    before = asked_as('hub.example.lan')
    try:
        code, body = api('POST', '/api/settings', 'hosts=' + urllib.parse.quote('esp-alarmhub.fritz.box, hub.example.lan'))
        check('other names for the hub can be saved, without a restart', code == 200 and json.loads(body).get('restart') is False, (code, body))
        check('and the hub answers to them at once', before == 421 and asked_as('hub.example.lan') == 200, before)
        check('a bad other name is refused', api('POST', '/api/settings', 'hosts=' + urllib.parse.quote('bad name!'))[0] == 400)
        check('more than four other names are refused', api('POST', '/api/settings', 'hosts=a,b,c,d,e')[0] == 400)
    finally:
        api('POST', '/api/settings', 'hosts=')


@case(pc=True)
def hub_firmware_must_be_signed(s):
    """Firmware for the hub: signed with its key, whole, valid, and no older than its own release."""
    check('firmware update needs the web password', api('PUT', '/api/firmware', b'\xe9' * 100, auth=False)[0] == 401)
    image = b'\xe9' + b'\x01' * 9000
    check('signed firmware is accepted', api('PUT', '/api/firmware', signed(image))[0] == 200)
    code, reply = api('PUT', '/api/firmware', image)
    check('firmware without a signature is refused', code == 400 and b'not signed' in reply, (code, reply[:80]))
    code, reply = api('PUT', '/api/firmware', other_key_signed(image))
    check('firmware signed with another key is refused', code == 400 and b'not signed' in reply, (code, reply[:80]))
    changed = bytearray(signed(image))
    changed[5000] ^= 1
    check('signed firmware changed afterwards is refused', api('PUT', '/api/firmware', bytes(changed))[0] == 400)
    check('signed firmware with its last byte cut off is refused', api('PUT', '/api/firmware', signed(image)[:-1])[0] == 400)
    check('a file shorter than a signature is refused', api('PUT', '/api/firmware', b'\xe9' * 50)[0] == 400)
    code, reply = api('PUT', '/api/firmware', signed(b'not firmware' * 500))
    check('other files are refused as firmware, signed or not', code == 400 and b'not valid firmware' in reply, (code, reply[:80]))
    check('and the next signed firmware is still accepted', api('PUT', '/api/firmware', signed(image))[0] == 200)
    # Never an older release than the hub's own (4000 here, as 0.4.0), whose faults may since have been fixed.
    code, reply = api('PUT', '/api/firmware', signed(image, 3999))
    check('an older release is refused, as older', code == 400 and b'older' in reply, (code, reply[:90]))
    check('a newer release is accepted', api('PUT', '/api/firmware', signed(image, 4001))[0] == 200)
    code, reply = api('PUT', '/api/firmware', signed(image, None))
    check('a signed firmware from before release numbers is refused, as older', code == 400 and b'older' in reply, (code, reply[:90]))
    raised = bytearray(signed(image, 3999))
    raised[len(image)] = 0xFF  # its number raised after signing
    check('a release number changed after signing is refused', api('PUT', '/api/firmware', bytes(raised))[0] == 400)


@case(pc=True)
def no_update_while_a_new_firmware_proves_itself(s):
    """A firmware just put on is on trial, and goes back to the one before should it fail. An update then would write
    over that one, and before the update's signature is checked, so it is refused, saying why."""
    s.restart(env={'S2O_FIRMWARE_ON_TRIAL': '1'})
    try:
        code, reply = api('PUT', '/api/firmware', signed(b'\xe9' + b'\x01' * 9000))
        check('an update is refused while the firmware is on trial', code == 409 and b'proving itself' in reply, (code, reply[:120]))
        check('and the hub still answers', get_json('/api/info') is not None)
    finally:
        s.restart()
    check('once it has proved itself, an update is taken', api('PUT', '/api/firmware', signed(b'\xe9' + b'\x01' * 9000))[0] == 200)


@case(pc=True)
def onvif_and_rtsp_logins(s):
    """ONVIF and RTSP with the login on: digest and plain, wrong passwords and users, and the snapshot's token."""
    control('auth-on')
    try:
        check('ONVIF needs a login', soap('GetProfiles')[0] == 400 and 'NotAuthorized' in soap('GetProfiles')[1])
        check('the clock is open before login', soap('GetSystemDateAndTime', DEVICE_SERVICE)[0] == 200)
        check('ONVIF accepts a digest login', soap_login('GetProfiles', 'onvif-secret')[0] == 200)
        check('ONVIF accepts a plain login', soap_login('GetProfiles', 'onvif-secret', digest=False)[0] == 200)
        check('ONVIF refuses a wrong password', soap_login('GetProfiles', 'wrong')[0] == 400)
        check('ONVIF refuses a wrong user', soap_login('GetProfiles', 'onvif-secret', user='bob')[0] == 400)
        # A login sent again as it was, nonce and all, is counted: for now still let in, until the NVR's are known not to repeat.
        before = get_json('/api/info')['onvifRepeats']
        body = (f'<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">{wsse_header("onvif-secret")}'
                '<s:Body><GetProfiles xmlns="x"/></s:Body></s:Envelope>')
        codes = []
        for _ in range(2):
            c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
            c.request('POST', '/onvif/media', body, {'Content-Type': 'application/soap+xml'})
            r = c.getresponse()
            r.read()
            c.close()
            codes.append(r.status)
        after = get_json('/api/info')['onvifRepeats']
        check('a login sent again with the same nonce is counted, and still let in for now', codes == [200, 200] and after == before + 1, (codes, before, after))
        check('fresh logins are not counted', soap_login('GetProfiles', 'onvif-secret')[0] == 200 and get_json('/api/info')['onvifRepeats'] == after)
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
        sock, code, _ = rtsp_describe('Basic ' + base64.b64encode(b'admin:onvif-secret').decode())
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
    finally:
        control('auth-off')
    check('ONVIF is open again without a password', soap('GetProfiles')[0] == 200)


@case()
def pull_point(s):
    """Clients that poll for events instead: told the state once, then only of a change; a poll with a timeout waits for
    one; and only the subscription's maker can pull from it or end it."""
    code, data = soap('CreatePullPointSubscription', '/onvif/events')
    address = re.search(r'/onvif/sub/\d+', data).group(0)
    check('pull point delivers the state once', 'Value="false"' in soap('PullMessages', address)[1])
    check('pull point is quiet until a change', 'NotificationMessage' not in soap('PullMessages', address)[1])
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
    # Another device cannot pull from or end the subscription: pulling it would take the change the NVR waits for.
    if not DEVICE:  # needs a second address on this computer
        get(f'/sensor?{sid(3)}')
        other = soap('PullMessages', address, source='127.0.0.2')
        check("another address pulling the NVR's subscription learns nothing", 'NotificationMessage' not in other[1], other)
        check("another address cannot end the NVR's subscription", soap('Unsubscribe', address, source='127.0.0.2')[0] == 400)
        own = soap('PullMessages', address)
        check('the NVR still gets the change', 'Value="true"' in own[1], own)
        clear(3)
    soap('Unsubscribe', address)


@case()
def ws_discovery(s):
    """WS-Discovery, sent directly."""
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(3)
    probe = ('<e:Envelope xmlns:e="http://www.w3.org/2003/05/soap-envelope" xmlns:w="a" xmlns:d="b" xmlns:dn="c"><e:Header><w:MessageID>uuid:test-1</w:MessageID>'
             '</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body></e:Envelope>')
    udp.sendto(probe.encode(), (HOST, DISCOVERY))
    reply = udp.recv(4096).decode()
    udp.close()
    check('discovery answers a Probe', 'ProbeMatch' in reply and 'uuid:test-1' in reply and f'http://{HOST}:{ONVIF}/onvif/device_service' in reply, reply[:300])


@case()
def a_slightly_old_report_is_live(s):
    """A report a few seconds old still counts as live."""
    get(f'/sensor?{sid(6)}&age=5')
    now = status()
    check('recent report is treated as live', now['active'] == [name(6)], now)
    clear(6)
    newest = [e for e in json.loads(get('/events')[1]) if e['sensor'] == name(6) and e['active']][0]
    check('live report is not marked delayed', newest['delayed'] is False, newest)


@case()
def past_events(s):
    """Alerts that arrive late: the past-event picture, one NVR record each, and their true time in the event list."""
    nvr = s.nvr
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


@case()
def a_node_past_event_time(s):
    """Each node sets how old an alert must be to count as a past event, and when it counts as silent. Sensor 3 has no
    pictures."""
    node3 = SENSORS[3][0]

    def node_row():
        return [x for x in get_json('/api/tree')['nodes'] if x['mac'] == node3][0]
    try:
        check('a node can have its own past-event time', api('POST', f'/api/node-settings?path={node3}', 'late=300')[0] == 200)
        check('the Sensors page data shows it', node_row()['late'] == 300)
        check("it is kept in the node's folder", api('GET', f'/api/file?path={node3}/node.txt') == (200, b'late=300\n'))
        get(f'/sensor?{sid(3)}&age=100')
        wait_until(lambda: status()['active'] == [name(3)])
        check("an alert younger than the node's time is live", status()['active'] == [name(3)] and json.loads(get('/events')[1])[0]['delayed'] is False, json.loads(get('/events')[1])[0])
        colour = grab_colour()
        check('a live alert with no pictures shows the built-in alert', colour == 'red', colour)
        clear(3)
        get(f'/sensor?{sid(3)}&age=400')
        wait_until(lambda: status()['active'] == [name(3)])
        check("an alert older than the node's time is a past event", json.loads(get('/events')[1])[0]['delayed'] is True and status()['active'] == [name(3)], json.loads(get('/events')[1])[0])
        colour = grab_colour()
        check('a past event with no pictures shows the built-in past-event picture', colour == 'amber', colour)
        clear(3)
        check('a bad past-event time is refused', api('POST', f'/api/node-settings?path={node3}', 'late=0')[0] == 400)
        check('a node can have its own silent time', api('POST', f'/api/node-settings?path={node3}', 'late=300&silent=600')[0] == 200
              and api('GET', f'/api/file?path={node3}/node.txt') == (200, b'late=300\nsilent=600\n'))
        check('the Sensors page data shows it', node_row()['silent'] == 600)
        check('a silent time under a minute is refused', api('POST', f'/api/node-settings?path={node3}', 'silent=30')[0] == 400)
        check('settings for a node that does not exist are refused', api('POST', '/api/node-settings?path=02-00-00-00-00-77', 'late=60')[0] == 404)
        check('clearing the node settings removes the file', api('POST', f'/api/node-settings?path={node3}', '')[0] == 200 and api('GET', f'/api/file?path={node3}/node.txt')[0] == 404)
        api('PUT', f'/api/file?path={node3}/settings.txt', b'late=120\n')  # as an older backup restores it
        check("a node's settings.txt from an older backup is read", node_row()['late'] == 120)
        check('and taken into node.txt, the old file removed', api('GET', f'/api/file?path={node3}/node.txt') == (200, b'late=120\n')
              and api('GET', f'/api/file?path={node3}/settings.txt')[0] == 404)
        api('POST', f'/api/node-settings?path={node3}', '')
        get(f'/sensor?{sid(3)}&age=100')
        wait_until(lambda: event_list()[0]['active'])
        check('without the setting, 100 seconds is a past event again', json.loads(get('/events')[1])[0]['delayed'] is True)
        clear(3)
    finally:
        api('POST', f'/api/node-settings?path={node3}', '')


@case()
def a_hung_listener(s):
    """A listener that accepts and never answers must not stall the camera or the good subscriber."""
    hung = socket.socket()
    hung.bind((LOCAL, 0))
    hung.listen(8)
    reply = soap('Subscribe', DEVICE_SERVICE, f'<ConsumerReference><Address>http://{LOCAL}:{hung.getsockname()[1]}/x</Address></ConsumerReference>')[1]
    try:
        before = len(s.nvr.events)
        get(f'/sensor?{sid(1)}')
        slowest = 0
        for _ in range(15):
            t = time.time()
            code = soap('GetSystemDateAndTime', DEVICE_SERVICE)[0]
            slowest = max(slowest, time.time() - t)
            time.sleep(0.1)
        check('camera keeps answering with a hung listener', code == 200 and slowest < 0.5, slowest)
        check('good subscriber still gets events with a hung one present', 'true' in s.nvr.states()[before:], s.nvr.states()[before:])
        clear(1)
        check('still answering at the end', soap('GetSystemDateAndTime', DEVICE_SERVICE)[0] == 200)
    finally:
        unsubscribe(reply)
        hung.close()


@case(pc=True)
def a_push_answered_with_an_error_is_sent_again(s):
    """A listener that answers a push with an HTTP error did not take it, so the hub counts it failed and sends it again,
    as it would one that could not connect. One that closes without a reply, as the NVR does, gets it once."""
    refusing = FakeNvrListener(reply=b'HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
    reply = soap('Subscribe', DEVICE_SERVICE, f'<ConsumerReference><Address>http://{LOCAL}:{refusing.port}/x</Address></ConsumerReference>')[1]
    try:
        wait_until(lambda: refusing.events)
        wait_until(lambda: len(refusing.events) >= 2, timeout=40)  # the hub tries a failed push again every 30 s
        check('a push answered with HTTP 500 is sent again', len(refusing.events) >= 2, len(refusing.events))
        # Its first message never got there, so the one sent again is still the subscription's first.
        check('a first push that was refused is sent again as Initialized', 'PropertyOperation="Initialized"' in refusing.events[1][1])
    finally:
        unsubscribe(reply)


@case(pc=True)
def pushes_wait_for_sockets(s):
    """Web connections and pushes share a few sockets, so a push may have to wait for one. It must still go out."""
    second = FakeNvrListener()
    reply = soap('Subscribe', DEVICE_SERVICE, f'<ConsumerReference><Address>http://{LOCAL}:{second.port}/x</Address></ConsumerReference>')[1]
    try:
        wait_until(lambda: second.events)  # its first state, so what follows is the alert's
        first_seen, second_seen = len(s.nvr.events), len(second.events)
        idle = [socket.create_connection((HOST, WEB), timeout=5) for _ in range(5)]
        time.sleep(0.2)  # the hub accepts the idle connections, so they hold sockets
        get(f'/sensor?{sid(1)}')
        time.sleep(2)  # the push must get out while the sockets stay taken
        for c in idle:
            c.close()
        check('pushes that wait for a socket still go out', len(s.nvr.events) > first_seen and len(second.events) > second_seen,
              (len(s.nvr.events), first_seen, len(second.events), second_seen))
        clear(1)
    finally:
        unsubscribe(reply)


@case()
def the_sensor_limit(s):
    """The limit of 32 is refused up front. Everything made here is under a node this test owns, and is removed again."""
    extra = '02-00-00-00-33-33'
    free = 32 - status()['sensors']
    try:
        made = [api('POST', f'/api/mkdir?path={extra}/{i + 1}')[0] for i in range(free)]
        check('sensor folders up to the limit are accepted', set(made) <= {200}, made)
        check('a sensor folder beyond the limit is refused', api('POST', f'/api/mkdir?path={extra}/{free + 1}')[0] == 409)
        check('a name for a sensor beyond the limit is refused', api('PUT', f'/api/file?path={extra}/{free + 1}/sensor.txt', b'name=x\n')[0] == 409)
        if free:
            check('an existing sensor can still be changed at the limit', api('PUT', f'/api/file?path={extra}/1/sensor.txt', b'name=x\n')[0] == 200)
    finally:
        api('DELETE', f'/api/file?path={extra}')
    check('the limit frees up when sensors are deleted', status()['sensors'] == 32 - free, status())


@case()
def a_deleted_sensors_past_event(s):
    """A past event belongs to its device: a new sensor that takes a deleted one's place in the table must not play it."""
    gone, newcomer = '02-00-00-00-44-01', '02-00-00-00-44-02'
    api('POST', f'/api/mkdir?path={gone}/1')
    try:
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
        # Until the queue is empty, then through the time it would have played for, since it must not appear.
        poll(watched, lambda st: st['delayed_waiting'] == 0, timeout=2 * (HOLD_MS / 1000 + 4) + 5, step=0.2)
        end = time.time() + HOLD_MS / 1000 + 1
        while time.time() < end:
            watched()
            time.sleep(0.2)
        check("a deleted sensor's delayed alert does not play as another sensor", f'{newcomer}/1' not in shown and status()['delayed_waiting'] == 0, shown)
    finally:
        api('DELETE', f'/api/file?path={gone}')
        api('DELETE', f'/api/file?path={newcomer}')


@case()
def late_alerts_from_one_sensor(s):
    """Late alerts from one sensor close together are one record, as they would have been live."""
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


@case(last=True)
def deleting_folders_and_restarts(s):
    """Deleting folders removes the sensors without a restart; and what a restart keeps, logs and plays."""
    count = status()['sensors']
    check('sensor folder can be deleted', api('DELETE', f'/api/file?path={name(6)}')[0] == 200)
    check('deleted sensor is unknown at once', get(f'/sensor?{sid(6)}')[0] == 404 and status()['sensors'] == count - 1)
    check('node folder can be deleted with its contents', api('DELETE', f'/api/file?path={A}')[0] == 200)
    check('all its sensors are gone', get(f'/sensor?{sid(1)}')[0] == 404 and status()['sensors'] == count - 4)
    if s.proc:
        # A planned restart writes the waiting alerts as they are, so an alert the NVR already got does not play again.
        get(f'/sensor?{sid(4)}')
        wait_until(lambda: name(4) not in status()['active'], timeout=20)  # its hold runs out, after its push showed it to the NVR
        control('save-now')
        s.restart('1500')
        time.sleep(3)  # time for a stale entry to wrongly start playing
        check('after a planned restart, an alert the NVR got does not play again', name(4) not in status()['active'] and status()['delayed_waiting'] == 0, status())

        # A past event still waiting when the power goes must play after the restart: its sensor was told it was received.
        get(f'/sensor?{sid(4)}')  # keeps it waiting
        get(f'/sensor?{sid(5)}&age=500')
        check('a past event waits while another alert is on', status()['delayed_waiting'] >= 1, status())
        s.restart('1500')
        check('folders are still sensors after a restart', status()['sensors'] == count - 4, status())
        check('stored pictures survive a restart', api('GET', f'/api/file?path={name(4)}/active.h264')[1] == s.files['active.h264'])
        check('a past event waiting at the restart still plays', wait_until(lambda: name(5) in status()['active']), status())
        control('end-alerts')
        quiet_nodes = poll(lambda: get_json('/api/espnow')['nodes'], lambda n: n and all(x['state'] == 'silent' for x in n))
        check('a node quiet since the restart is reported silent', quiet_nodes and all(n['state'] == 'silent' for n in quiet_nodes), quiet_nodes)
        check('going silent after a restart is logged', any(e['kind'] == 1 for e in get_json('/api/events')))
        starts = [e for e in event_list() if e['kind'] == 8]
        check('the start is logged with its reason and how long the hub was off', starts and starts[0]['sensor'] == 'hub' and starts[0]['detail'] == 'PC build started'
              and starts[0]['event'] == 'hub started' and starts[0]['off_s'] is not None and starts[0]['off_s'] < 3600, starts[:2])
        # A third start soon after the last two, none of them asked for: the hub counts them as restarting often.
        s.restart('1500')
        loop = poll(event_list, lambda l: any(e['kind'] == 16 for e in l))
        loop = [e for e in loop if e['kind'] == 16]
        check('three unplanned starts within an hour are logged as restarting often', loop and loop[0]['sensor'] == 'hub'
              and any(e['detail'] == '3 times in an hour' for e in loop), loop)
    api('DELETE', f'/api/file?path={B}')
