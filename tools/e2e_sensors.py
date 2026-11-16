"""Sensor reports and what follows from them: signatures, health, settings, the event history, notifications and memory."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
import e2e_common as common
from e2e_mqtt_arm import arm_tests, mqtt_tests
from e2e_nodes import pairing_tests


def test_replacement_all_clear(files):
    # A replacement "all clear" takes effect at once, and deleting it restores the built-in one.
    check('clear.h264 can be uploaded', api('PUT', '/api/file?path=clear.h264', files['active.h264'])[0] == 200)
    colour = grab_colour()
    check('uploaded clear picture is streamed', colour == 'red', colour)
    if PASSWORD:
        half = socket.create_connection((HOST, WEB), timeout=5)
        half.sendall(b'PUT /api/file?path=clear.h264 HTTP/1.1\r\nHost: x\r\nContent-Length: 30000\r\n\r\nabc')
        time.sleep(1.2)  # time for the hub to wrongly accept it
        check('an upload with no login leaves the picture alone', get_json('/api/live')['file'] == 'clear.h264')
        half.close()
    check('clear.h264 can be deleted', api('DELETE', '/api/file?path=clear.h264')[0] == 200)
    colour = grab_colour()
    check('built-in clear picture returns', colour == 'green', colour)
    return colour


def test_signed_messages(node_entry, radio):
    # Signed messages.
    info = radio()
    check('the Nodes and pairing page shows the key', info['key'] == 'test-espnow-key-0123456789', info)
    control('last-sent')
    control('radio-forged')
    check('a message signed with another key is refused', node_entry(radio())['refusal'] == 'wrong signature', node_entry(radio()))
    check('a refused message gets no confirmation', control('last-sent') == '')
    control('radio-known')
    before = node_entry(radio())
    control('radio-old')
    after = node_entry(radio())
    check('a signed message with a lower number is still accepted, so no alert is lost', after['refused'] == before['refused'] and after['counter'] == before['counter'] - 9, (before, after))
    events = len(get_json('/api/events'))
    control('radio-restart-alert')
    after = node_entry(radio())
    history = get_json('/api/events')
    check('a sensor that restarts its count at 1 is heard', after['counter'] == 1 and after['refused'] == before['refused'], after)
    check("a restarted sensor's alert reaches the NVR", name(1) in status()['active'] and history[0]['active'] is True and len(history) > events, history[:1])
    control('radio-restart-alert')
    check('the same report again a moment later is a resend and ignored', len(get_json('/api/events')) == len(history))
    clear(1)
    return before, events, history


def test_weak_signal(node_entry, radio):
    # Weak signal.
    control('radio-weak')
    check('a weak signal is flagged', node_entry(radio())['weak'] is True, node_entry(radio()))
    check('a weak signal is in the event history', get_json('/api/events')[0]['kind'] == 4)
    for _ in range(6):
        control('radio-known')
    check('a good signal clears the flag', node_entry(radio())['weak'] is False, node_entry(radio()))
    clear(1)


def test_battery_silence_alerts(label, node_entry, radio):
    # Battery and silence alerts.
    control('radio-low')
    check('low battery is flagged', node_entry(radio())['low'] is True, node_entry(radio()))
    history = get_json('/api/events')
    check('low battery is in the event history', history and history[0]['kind'] == 2 and history[0]['name'] == label.decode(), history[:1])
    before_again = len(get_json('/api/events'))
    control('radio-low')
    check('low battery alerts only once', len(get_json('/api/events')) == before_again)
    control('radio-known')
    check('a good battery clears the flag', node_entry(radio())['low'] is False, node_entry(radio()))
    check('the alarm setting is saved', api('POST', '/api/alerts', 'alarm=1')[0] == 200 and get_json('/api/alerts')['alarm'] is True)
    control('radio-low')
    wait_until(lambda: node_entry(radio())['state'] == 'active')
    check('a low battery can raise the alarm', node_entry(radio())['state'] == 'active', node_entry(radio()))
    clear(1)
    control('radio-known')
    api('POST', '/api/alerts', 'alarm=0')


def test_per_sensor_settings():
    # Per-sensor settings.
    path = name(1)

    def tree_entry():
        return [n for s in get_json('/api/tree')['nodes'] if s['mac'] == SENSORS[1][0] for n in s['sensors'] if n['input'] == SENSORS[1][1]][0]

    check('a sensor can have its own hold', api('POST', f'/api/sensor-settings?path={path}', 'hold=2')[0] == 200)
    check('the Sensors page data shows the hold', tree_entry()['hold'] == 2 and tree_entry()['quiet'] is None, tree_entry())
    get(f'/sensor?{sid(1)}')
    time.sleep(0.5)  # timing the 2-second hold: still on at 0.5 s, off by 3 s
    active_soon = path in status()['active']
    time.sleep(2.5)
    check("the alert ends after the sensor's own hold", active_soon and path not in status()['active'])
    check('a bad hold is refused', api('POST', f'/api/sensor-settings?path={path}', 'hold=0')[0] == 400)
    check('quiet hours need two different times', api('POST', f'/api/sensor-settings?path={path}', 'from=60&to=60')[0] == 400)
    check('settings for a sensor that does not exist are refused', api('POST', '/api/sensor-settings?path=02-00-00-00-00-77/1', 'hold=5')[0] == 404)
    # Input 257 is not input 1 cut down to fit: an alert never lands on another sensor.
    for bad in ('257', '0', '1x', '+1', ' 1'):
        st, reply = get(f'/sensor?mac={SENSORS[1][0]}&input={bad.replace(" ", "%20")}')
        check(f'a sensor report with input {bad!r} is refused', st == 400 and 'input must be' in reply, (st, reply))
    c = http.client.HTTPConnection(HOST, WEB, timeout=5)
    c.putrequest('POST', '/api/walk-test')
    c.putheader('Content-Length', '12x')
    c.endheaders()
    r = c.getresponse()
    r.read()
    c.close()
    check('a Content-Length that is not a number is refused', r.status == 400, r.status)
    minute = int(time.time() // 60 % 1440)
    api('POST', f'/api/sensor-settings?path={path}', f'from={(minute - 2) % 1440}&to={(minute + 3) % 1440}')
    check('the Sensors page data shows the quiet hours', tree_entry()['quiet'] == [(minute - 2) % 1440, (minute + 3) % 1440], tree_entry())
    get(f'/sensor?{sid(1)}')
    wait_until(lambda: event_list()[0]['kind'] == 3)  # logged, so the hub has decided
    check('an alert in quiet hours does not reach the NVR', path not in status()['active'])
    check('an alert in quiet hours is still logged', get_json('/api/events')[0]['kind'] == 3)
    code, text = api('GET', f'/api/file?path={path}/settings.txt')
    check("the settings are kept in the sensor's folder", code == 200 and text.startswith(b'quiet='), text)
    check('clearing the settings removes the file', api('POST', f'/api/sensor-settings?path={path}', '')[0] == 200 and api('GET', f'/api/file?path={path}/settings.txt')[0] == 404)
    api('PUT', f'/api/file?path={path}/settings.txt', b'hold=3\n')
    check('settings.txt restored as a file is read too', tree_entry()['hold'] == 3, tree_entry())
    api('POST', f'/api/sensor-settings?path={path}', '')
    get(f'/sensor?{sid(1)}')
    wait_until(lambda: path in status()['active'])
    check('without quiet hours alerts reach the NVR again', path in status()['active'])
    clear(1)
    return code, path


def test_full_event_history(path):
    # The full event history.
    code, csv = api('GET', '/api/events.csv')
    lines = csv.decode().splitlines()
    check('the full history downloads as CSV', code == 200 and lines[0].startswith('received_utc,') and any(f',{path},' in l and ',alert in quiet hours,' in l for l in lines), lines[:3])
    check('the history is sent in chunks, so a download cut short shows as failed', common.last_transfer == 'chunked', common.last_transfer)
    check('a name with quotes stays one CSV field', any('"Front ""gate"""' in l for l in lines), [l for l in lines if path in l][:2])
    check('the full history needs the web password', api('GET', '/api/events.csv', auth=False)[0] == 401)
    # One sensor's last days, as each sensor's page asks for.
    one_code, one = api('GET', f'/api/events.csv?sensor={path}&days=8')
    one_lines = one.decode().splitlines()
    check("one sensor's last days hold only its events", one_code == 200 and len(one_lines) > 1 and all(f',{path},' in l for l in one_lines[1:]),
          one_lines[:3])
    check('days out of range are refused', api('GET', '/api/events.csv?days=0')[0] == 400)
    check('the Long Range radio test is only on a board with a radio', api('GET', '/api/radio-test')[0] == 404)
    return code


def test_webhook(path):
    # Webhook.
    received = []

    class Hook(http.server.BaseHTTPRequestHandler):
        def do_POST(self):
            received.append((self.headers.get('Content-Type'), self.rfile.read(int(self.headers['Content-Length']))))
            self.send_response(200)
            self.send_header('Content-Length', '0')
            self.end_headers()

        def log_message(self, *args):
            pass

    catcher = http.server.HTTPServer(('127.0.0.1', 0), Hook)
    threading.Thread(target=catcher.serve_forever, daemon=True).start()
    hook_url = urllib.parse.quote(f'http://127.0.0.1:{catcher.server_port}/hub', safe='')

    def hooks(count):
        for _ in range(50):
            if len(received) >= count:
                return True
            time.sleep(0.1)
        return False

    check('a webhook address with a name is refused', api('POST', '/api/webhook', 'url=' + urllib.parse.quote('http://example.com/x', safe='') + '&alerts=1')[0] == 400)
    check('an https webhook is refused', api('POST', '/api/webhook', 'url=' + urllib.parse.quote('https://192.168.1.2/x', safe='') + '&alerts=1')[0] == 400)
    check('the webhook is saved', api('POST', '/api/webhook', f'url={hook_url}&alerts=1&problems=1')[0] == 200)
    api('POST', '/api/webhook/test', 'x=1')
    check('a test message arrives', hooks(1) and json.loads(received[0][1])['event'] == 'test', received)
    get(f'/sensor?{sid(1)}')
    alert = json.loads(received[1][1]) if hooks(2) else {}
    check('an alert is sent to the webhook', alert.get('event') == 'alert' and alert.get('sensor') == path and alert.get('name') == 'Front "gate"', received[1:])
    clear(1)
    hook_state = poll(lambda: get_json('/api/webhook'), lambda h: h['waiting'] == 0 and h['last'])
    check('the page shows how the last message went', hook_state['last'] == 'HTTP 200' and hook_state['waiting'] == 0, hook_state)
    api('POST', '/api/webhook', f'url={hook_url}&text=1&alerts=1&problems=0')
    get(f'/sensor?{sid(1)}')
    check('plain text is sent when chosen', hooks(3) and received[2][0].startswith('text/plain') and received[2][1].decode().endswith(': alert'), received[2:])
    clear(1)
    control('radio-low')
    time.sleep(0.5)  # time for a message to be wrongly sent
    check('node problems are not sent when turned off', len(received) == 3, received[3:])
    control('radio-known')
    if not DEVICE:  # these need the PC build's test commands
        test_trouble_reports(received, hook_url)
    check('the webhook settings need the web password', api('GET', '/api/webhook', auth=False)[0] == 401)
    api('POST', '/api/webhook', 'url=&alerts=1&problems=1')
    catcher.shutdown()
    check('the event history needs the web password', api('GET', '/api/events', auth=False)[0] == 401)
    check('/status needs the web password', http_status('/status') == 401)
    control('memory-short')
    code = api('GET', '/api/events')[0]
    control('memory-normal')
    check('a reply larger than the memory left fails alone, and the hub keeps running', code == 507 and api('GET', '/api/events')[0] == 200, code)
    return code, received


def test_trouble_reports(received, hook_url):
    """The daily check, a sensor firing too often, and nodes going quiet together, with the webhook catching what is sent."""
    def sent(event):
        found = []
        for _, body in list(received):
            try:
                message = json.loads(body)
            except ValueError:
                continue
            if message.get('event') == event:
                found.append(message)
        return found

    # The daily check, set for this minute: the PC build's local time is UTC.
    if time.gmtime().tm_sec > 54:
        time.sleep(7)  # not at the end of a minute, which would be over before the hub looks
    now = time.gmtime()
    minute = now.tm_hour * 60 + now.tm_min
    check('a daily check time past midnight is refused', api('POST', '/api/webhook', f'url={hook_url}&alerts=1&problems=1&daily=1440')[0] == 400)
    check('the daily check time is saved', api('POST', '/api/webhook', f'url={hook_url}&alerts=1&problems=1&daily={minute}')[0] == 200
          and get_json('/api/webhook')['daily'] == minute)
    daily = wait_until(lambda: sent('daily check'), timeout=5)
    check('the daily check arrives at its time', len(daily) == 1 and daily[0]['nodes'] >= 2 and ' daily check: ' in daily[0]['message']
          and 'nvrListening' in daily[0], daily)
    time.sleep(1)  # time for a second one to be wrongly sent
    check('and only once', len(sent('daily check')) == 1)
    api('POST', '/api/webhook', f'url={hook_url}&alerts=1&problems=1')
    check('no time turns the daily check off', get_json('/api/webhook')['daily'] == -1)

    # A sensor firing far too often is reported once; every alert still goes through.
    control('busy-alerts 3')
    for _ in range(4):
        get(f'/sensor?{sid(6)}')
        clear(6)
    control('busy-alerts 100000')
    busy = [e for e in event_list() if e['event'] == 'firing often' and e['sensor'] == name(6)]
    check('a sensor firing often is logged once', len(busy) == 1, busy)
    check('and sent as a node problem', wait_until(lambda: sent('firing often'), timeout=5))

    # Nodes that report steadily and then stop together.
    nodes = (SENSORS[1][0], SENSORS[4][0])
    for folder in nodes:
        api('POST', f'/api/node-settings?path={folder}', 'every=600')
    for n in (1, 4):
        get(f'/sensor?{sid(n)}')
        clear(n)
    control('overdue-ms 1500')  # instead of 12 minutes for a node that reports every 10
    jam = poll(lambda: [e for e in event_list() if e['event'] == 'possible jamming'], lambda found: found, timeout=6)
    check('nodes going quiet together are logged as possible jamming', len(jam) == 1 and jam[0]['sensor'] == 'hub' and 'nodes' in jam[0]['detail'], jam)
    check('and sent as a node problem', wait_until(lambda: sent('possible jamming'), timeout=5))
    time.sleep(1)
    check('once', len([e for e in event_list() if e['event'] == 'possible jamming']) == 1)
    control('overdue-ms 0')
    for folder in nodes:
        api('POST', f'/api/node-settings?path={folder}', '')


def test_no_memory_left_at_all():
    # No memory left at all: a radio alert still starts, is logged and is confirmed, and the hub keeps running.
    dropped = int(control('memory-failures'))
    control('last-sent')
    control('memory-gone')
    control('radio-alert')
    time.sleep(0.3)  # passes of the hub's loop with no memory at all
    control('memory-normal')
    check('an alert with no memory left reaches the NVR, and the hub keeps running', name(1) in status()['active'], status())
    newest = event_list()[0]
    check('an alert with no memory left is logged', newest['sensor'] == name(1) and newest['active'] is True and newest['kind'] == 0, newest)
    sent = wait_until(lambda: control('last-sent'))
    check('an alert with no memory left is confirmed once saved', sent.startswith('020000000001 030101'), sent)
    check('the hub counts the work it dropped for lack of memory', int(control('memory-failures')) > dropped)
    clear(1)


def test_events_list_tag():
    # The Events page asks every 3 seconds; an unchanged list is answered without being built again.
    def events_tagged(tag=None):
        headers = {'Authorization': 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()} if PASSWORD else {}
        if tag:
            headers['If-None-Match'] = tag
        c = http.client.HTTPConnection(HOST, WEB, timeout=15)
        c.request('GET', '/api/events', None, headers)
        r = c.getresponse()
        r.read()
        c.close()
        return r.status, r.getheader('ETag')

    code, tag = events_tagged()
    check('the event list carries a tag', code == 200 and tag, tag)
    check('an unchanged event list is not sent again', events_tagged(tag) == (304, tag), events_tagged(tag))
    get(f'/sensor?{sid(1)}')
    clear(1)
    check('a new event changes the tag', events_tagged(tag)[0] == 200)

    mqtt_tests(lambda n: get(f'/sensor?{sid(n)}'), clear)
    arm_tests()
    if not DEVICE:  # the board's radio cannot be driven from here
        pairing_tests()
    return code


def test_alerts_the_nvr_missed(path):
    # Alerts the NVR did not get. Nothing subscribes from 192.0.2.8, so with that as the NVR's address it is not listening.
    api('POST', '/api/nvr', 'ip=192.0.2.8')
    control('nvr-gone-fast')  # the NVR counts as gone after 3 s here, instead of 3 minutes
    get(f'/sensor?{sid(1)}')
    missed = poll(event_list, lambda l: l and any(e['kind'] == 5 for e in l[:2]))
    check('an alert the NVR is not listening for is logged as not delivered', missed and any(e['kind'] == 5 and e['sensor'] == path and e['event'] == 'alert not delivered' for e in missed[:2]), missed[:3])
    gone = poll(event_list, lambda l: any(e['kind'] == 6 for e in l[:3]))
    check('the NVR not listening is logged as a hub event', [e['sensor'] for e in gone[:3] if e['kind'] == 6] == ['hub'], gone[:3])
    clear(1)
    soap('CreatePullPointSubscription', '/onvif/events')  # this PC now listens, as an NVR that pulls events would
    api('POST', '/api/nvr', 'ip=127.0.0.1')
    back = poll(event_list, lambda l: l and any(e['kind'] == 7 for e in l[:2]))
    check('the NVR listening again is logged', back and any(e['kind'] == 7 and e['sensor'] == 'hub' for e in back[:2]), back[:2])
    check('the alert the NVR missed plays once it listens again, as a past event', wait_until(lambda: name(1) in status()['active'], timeout=15), status())
    return gone


def test_nvr_goes_again_during_replay():
    # The NVR goes again during the replay: the replay waits again, without a second entry or message.
    missed_before = sum(e['kind'] == 5 for e in event_list())
    api('POST', '/api/nvr', 'ip=192.0.2.8')
    control('end-alerts')
    time.sleep(0.5)  # time for the check to run and wrongly log it again
    check('a replay the NVR misses again is not logged a second time', sum(e['kind'] == 5 for e in event_list()) == missed_before, event_list()[:3])
    api('POST', '/api/nvr', 'ip=127.0.0.1')
    check('and it plays again when the NVR is back', wait_until(lambda: name(1) in status()['active'], timeout=15), status())
    control('end-alerts')
    flagged = sum(e['kind'] == 5 for e in event_list())
    get(f'/sensor?{sid(1)}')
    time.sleep(6)  # past the check, which comes 5 s after an alert starts
    check('an alert the NVR is listening for is not flagged', sum(e['kind'] == 5 for e in event_list()) == flagged, event_list()[:3])
    clear(1)
    # A past event waits while the NVR is not listening, rather than playing to nobody and being logged as missed.
    missed = sum(e['kind'] == 5 for e in event_list())
    api('POST', '/api/nvr', 'ip=192.0.2.8')
    get(f'/sensor?{sid(2)}&age=500')
    time.sleep(7)  # longer than a play and its delivery check would take
    check('a past event waits while the NVR is not listening', name(2) not in status()['active'] and status()['delayed_waiting'] >= 1
          and sum(e['kind'] == 5 for e in event_list()) == missed, status())
    api('POST', '/api/nvr', 'ip=127.0.0.1')
    check('and plays once it listens again', wait_until(lambda: name(2) in status()['active'], timeout=15), status())
    control('end-alerts')
    control('nvr-gone-normal')
    api('POST', '/api/nvr', 'ip=')


def test_report_waits_for_busy_connections():
    # A report that arrives while every web connection is busy waits for one, instead of being turned away.
    idle = [socket.create_connection((HOST, WEB)) for _ in range(6)]
    time.sleep(0.3)  # time for the hub to take them all
    result = {}
    sender = threading.Thread(target=lambda: result.update(code=get(f'/sensor?{sid(1)}')[0]))
    sender.start()
    time.sleep(1)  # the report waits meanwhile
    idle.pop().close()
    sender.join(15)
    for s in idle:
        s.close()
    check('a report sent while every connection is busy waits, and arrives', result.get('code') == 200, result)
    clear(1)
    return idle, s


def test_live_alert_interrupts_past_event():
    # A live alert never shares its NVR record with a past event: the past event stops and plays again later.
    get(f'/sensor?{sid(2)}&age=500')
    playing = wait_until(lambda: name(2) in status()['active'], timeout=20)
    get(f'/sensor?{sid(1)}')
    now_on = status()
    check('a live alert stops a past event that was playing', playing and now_on['active'] == [name(1)] and now_on['delayed_waiting'] >= 1, now_on)
    control('end-alerts')
    check('and the past event plays again once the live one is over', wait_until(lambda: name(2) in status()['active'], timeout=20), status())
    control('end-alerts')


def test_firmware_upload_pauses_the_hub():
    # During an upload the hub drops video and takes nothing new, so the update has the memory it needs.
    video, code, _ = rtsp_describe()
    video.settimeout(3)
    upload = socket.create_connection((HOST, WEB), timeout=15)
    login = base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()
    image = b'\xe9' + b'\x01' * 19999
    upload.sendall(f'PUT /api/firmware HTTP/1.1\r\nHost: {HOST}\r\nAuthorization: Basic {login}\r\nContent-Length: {len(image)}\r\n\r\n'.encode() + image[:2000])
    try:
        video_closed = video.recv(100) == b''
    except OSError:
        video_closed = True
    video.close()
    check('a firmware upload stops the video stream', code == 200 and video_closed, code)
    waiting = {}
    asker = threading.Thread(target=lambda: waiting.update(answer=http_status('/status')))
    asker.start()
    asker.join(1.5)
    check('a request during a firmware upload waits', asker.is_alive(), waiting)
    upload.sendall(image[2000:])
    upload_reply = upload.recv(200)
    upload.close()
    asker.join(15)
    check('it is answered once the upload ends', upload_reply.startswith(b'HTTP/1.1 200') and waiting.get('answer') == 401, (upload_reply[:40], waiting))
    check('the hub still answers after a firmware upload', status()['uptime_s'] >= 0)
    control('firmware-refused')
    try:
        code, reply = api('PUT', '/api/firmware', b'\xe9' + b'\x01' * (1300 * 1024))
    except OSError as e:
        code, reply = type(e).__name__, b''
    control('firmware-allowed')
    check('a refused firmware of full size gets its reason, not a reset', code == 507 and b'could not start the update' in reply, (code, reply[:80]))
    code, backup = api('GET', '/api/backup')
    check('backup can be downloaded', code == 200 and backup.startswith(b'sensorcam-backup 1'), code)
    check('backup needs the web password', api('GET', '/api/backup', auth=False)[0] == 401)
    check('backup can be restored', api('POST', '/api/restore', backup)[0] == 200)
    check('other files are refused as a backup', api('POST', '/api/restore', b'hello')[0] == 400)
    check('restore needs the web password', api('POST', '/api/restore', backup, auth=False)[0] == 401)
    return code, reply
