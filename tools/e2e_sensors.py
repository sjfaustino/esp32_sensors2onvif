"""Sensor reports and what follows from them: signatures, health, settings, the event history, notifications and memory."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
import e2e_common as common


class HookCatcher:
    """A pretend webhook receiver on this PC: each message's type and body (received), and its path and headers (heads)."""

    def __init__(self):
        self.received, self.heads = [], []
        catcher = self

        class Hook(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                catcher.heads.append((self.path, dict(self.headers)))
                catcher.received.append((self.headers.get('Content-Type'), self.rfile.read(int(self.headers['Content-Length']))))
                self.send_response(200)
                self.send_header('Content-Length', '0')
                self.end_headers()

            def log_message(self, *args):
                pass

        self.server = http.server.HTTPServer(('127.0.0.1', 0), Hook)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.base = f'http://127.0.0.1:{self.server.server_port}'
        self.url = urllib.parse.quote(self.base + '/hub', safe='')

    def wait_for(self, count):
        """True once count messages have come."""
        return bool(wait_until(lambda: len(self.received) >= count, timeout=5, step=0.1))

    def close(self):
        api('POST', '/api/webhook', 'url=&alerts=1&problems=1')
        self.server.shutdown()


def radio():
    return get_json('/api/espnow')


def node_entry(d):
    """Sensor 1's row on the Nodes and pairing page: the PC build's radio commands report as it."""
    return [n for n in d['nodes'] if n['node'] == SENSORS[1][0] and n['input'] == SENSORS[1][1]][0]


@case(free=True)
def all_clear_picture(s):
    """The picture is green when all is clear; a replacement "all clear" takes effect at once, and deleting it restores
    the built-in one."""
    colour = grab_colour()
    check('picture is green when clear', colour == 'green', colour)
    check('clear.h264 can be uploaded', api('PUT', '/api/file?path=clear.h264', s.files['active.h264'])[0] == 200)
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


@case(pc=True)
def signed_messages(s):
    """Reports signed with the key the hub knows, and what it does with a lower number or a restarted count."""
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


@case(pc=True)
def weak_signal(s):
    """A weak signal is flagged and logged, and cleared by good ones."""
    control('radio-weak')
    check('a weak signal is flagged', node_entry(radio())['weak'] is True, node_entry(radio()))
    check('a weak signal is in the event history', get_json('/api/events')[0]['kind'] == 4)
    for _ in range(6):
        control('radio-known')
    check('a good signal clears the flag', node_entry(radio())['weak'] is False, node_entry(radio()))
    clear(1)


@case(pc=True)
def battery_alerts(s):
    """A low battery is flagged and logged once, and can raise the alarm."""
    label = SENSOR_1_NAME.encode()
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


@case(pc=True)
def per_sensor_settings(s):
    """A sensor's own hold and quiet hours, kept in its sensor.txt with its name."""
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
    code, text = api('GET', f'/api/file?path={path}/sensor.txt')
    check("the settings are kept in the sensor's file, after its name", code == 200 and text.startswith(b'name=Front "gate"\nquiet='), text)
    check('clearing the settings keeps its name', api('POST', f'/api/sensor-settings?path={path}', '')[0] == 200
          and api('GET', f'/api/file?path={path}/sensor.txt') == (200, b'name=Front "gate"\n'))
    api('PUT', f'/api/file?path={path}/settings.txt', b'hold=3\n')
    check('settings.txt from an older backup is read too', tree_entry()['hold'] == 3, tree_entry())
    check('and taken into sensor.txt, the old file removed', api('GET', f'/api/file?path={path}/sensor.txt') == (200, b'name=Front "gate"\nhold=3\n')
          and api('GET', f'/api/file?path={path}/settings.txt')[0] == 404)
    api('POST', f'/api/sensor-settings?path={path}', '')
    get(f'/sensor?{sid(1)}')
    wait_until(lambda: path in status()['active'])
    check('without quiet hours alerts reach the NVR again', path in status()['active'])
    clear(1)


@case(pc=True)
def full_event_history(s):
    """The whole history as CSV, and one sensor's last days."""
    path = name(1)
    minute = int(time.time() // 60 % 1440)  # an alert in quiet hours, to look for in it
    api('POST', f'/api/sensor-settings?path={path}', f'from={(minute - 2) % 1440}&to={(minute + 3) % 1440}')
    get(f'/sensor?{sid(1)}')
    wait_until(lambda: event_list()[0]['kind'] == 3)
    api('POST', f'/api/sensor-settings?path={path}', '')
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


@case(pc=True)
def webhook(s):
    """Alerts and node problems sent to a webhook, as JSON or plain text."""
    path = name(1)
    hook = HookCatcher()
    try:
        check('a webhook address with a name is refused', api('POST', '/api/webhook', 'url=' + urllib.parse.quote('http://example.com/x', safe='') + '&alerts=1')[0] == 400)
        check('an https webhook is refused', api('POST', '/api/webhook', 'url=' + urllib.parse.quote('https://192.168.1.2/x', safe='') + '&alerts=1')[0] == 400)
        check('the webhook is saved', api('POST', '/api/webhook', f'url={hook.url}&alerts=1&problems=1')[0] == 200)
        api('POST', '/api/webhook/test', 'x=1')
        check('a test message arrives', hook.wait_for(1) and json.loads(hook.received[0][1])['event'] == 'test', hook.received)
        get(f'/sensor?{sid(1)}')
        alert = json.loads(hook.received[1][1]) if hook.wait_for(2) else {}
        check('an alert is sent to the webhook', alert.get('event') == 'alert' and alert.get('sensor') == path and alert.get('name') == 'Front "gate"', hook.received[1:])
        clear(1)
        hook_state = poll(lambda: get_json('/api/webhook'), lambda h: h['waiting'] == 0 and h['last'])
        check('the page shows how the last message went', hook_state['last'] == 'HTTP 200' and hook_state['waiting'] == 0, hook_state)
        api('POST', '/api/webhook', f'url={hook.url}&text=1&alerts=1&problems=0')
        get(f'/sensor?{sid(1)}')
        check('plain text is sent when chosen', hook.wait_for(3) and hook.received[2][0].startswith('text/plain') and hook.received[2][1].decode().endswith(': alert'), hook.received[2:])
        clear(1)
        control('radio-low')
        time.sleep(0.5)  # time for a message to be wrongly sent
        check('node problems are not sent when turned off', len(hook.received) == 3, hook.received[3:])
        control('radio-known')
    finally:
        hook.close()


@case(pc=True)
def webhook_formats(s):
    """The camera to look at, ntfy and Gotify, and the weekly summary."""
    path = name(1)
    hook = HookCatcher()
    try:
        # A plain-text message that starts as JSON does is still sent as text: the format goes with each message.
        api('POST', f'/api/sensor-settings?path={path}', 'name=%7BGate%7D')
        api('POST', '/api/webhook', f'url={hook.url}&format=text&alerts=1&problems=0')
        count = len(hook.received)
        get(f'/sensor?{sid(1)}')
        check('plain text from a sensor named like JSON is still sent as text', hook.wait_for(count + 1) and hook.received[count][0].startswith('text/plain')
              and hook.received[count][1].decode().startswith('{Gate}'), hook.received[count:])
        clear(1)
        api('POST', f'/api/sensor-settings?path={path}', 'name=' + urllib.parse.quote(SENSOR_1_NAME))

        # The camera that covers a sensor, named wherever its alerts are.
        camera = urllib.parse.quote('Camera 3, gate', safe='')
        check('a camera on two lines is refused', api('POST', f'/api/sensor-settings?path={path}', 'camera=Camera%0A3')[0] == 400)
        check('a camera name over 32 characters is refused', api('POST', f'/api/sensor-settings?path={path}', 'camera=' + 'x' * 33)[0] == 400)
        check('a sensor can name the camera that covers it', api('POST', f'/api/sensor-settings?path={path}', f'camera={camera}')[0] == 200)
        entry = [n for s in get_json('/api/tree')['nodes'] if s['mac'] == SENSORS[1][0] for n in s['sensors'] if n['input'] == SENSORS[1][1]][0]
        check('the Sensors page data shows the camera', entry['camera'] == 'Camera 3, gate', entry)
        kept = api('GET', f'/api/file?path={path}/sensor.txt')
        check("it is kept in the sensor's file, with its name", kept == (200, b'name=Front "gate"\ncamera=Camera 3, gate\n'), kept)
        api('POST', '/api/webhook', f'url={hook.url}&format=json&alerts=1&problems=0')
        count = len(hook.received)
        get(f'/sensor?{sid(1)}')
        alert = json.loads(hook.received[count][1]) if hook.wait_for(count + 1) else {}
        check('an alert names its camera to the webhook', alert.get('camera') == 'Camera 3, gate' and alert.get('message', '').endswith('; look at Camera 3, gate'),
              alert)
        newest = [e for e in json.loads(get('/events')[1]) if e['sensor'] == path][0]
        check('and on the Events page', newest.get('camera') == 'Camera 3, gate', newest)
        clear(1)
        api('POST', f'/api/sensor-settings?path={path}', '')
        check('a sensor without a camera has none in its events', 'camera' not in json.loads(get('/events')[1])[0])

        # ntfy: plain text to the topic, with a title, a priority and a tag in the headers, and its access token.
        check('ntfy needs a topic in the address', api('POST', '/api/webhook', 'format=ntfy&alerts=1&url=' + urllib.parse.quote(hook.base + '/', safe=''))[0] == 400)
        check('an unknown format is refused', api('POST', '/api/webhook', f'format=telegram&url={hook.url}')[0] == 400)
        check('a token with a space is refused', api('POST', '/api/webhook', f'format=ntfy&url={hook.url}&token=a%20b')[0] == 400)
        check('ntfy is saved with a token', api('POST', '/api/webhook', f'format=ntfy&url={hook.url}&token=tk_secret1&alerts=1&problems=1')[0] == 200)
        saved = get_json('/api/webhook')
        check('the page is told a token is saved, never the token', saved['format'] == 'ntfy' and saved['token'] is True and 'tk_secret1' not in json.dumps(saved), saved)
        count = len(hook.received)
        get(f'/sensor?{sid(1)}')
        if hook.wait_for(count + 1):
            where, h = hook.heads[count]
            body = hook.received[count][1].decode()
            check('an alert goes to the ntfy topic as text', where == '/hub' and hook.received[count][0].startswith('text/plain') and body.endswith(': alert'), (where, body))
            check('with a title, an urgent priority and a tag', h.get('Title', '').endswith(': alert: Front "gate"') and h.get('Priority') == '5'
                  and h.get('Tags') == 'rotating_light', h)
            check('and the access token', h.get('Authorization') == 'Bearer tk_secret1', h)
        else:
            check('an alert reaches ntfy', False)
        clear(1)
        check('saving again with the token left blank keeps it', api('POST', '/api/webhook', f'format=ntfy&url={hook.url}&alerts=1')[0] == 200
              and get_json('/api/webhook')['token'] is True)
        check('the token can be forgotten', api('POST', '/api/webhook', f'format=ntfy&url={hook.url}&alerts=1&forget=1')[0] == 200
              and get_json('/api/webhook')['token'] is False)
        count = len(hook.received)
        api('POST', '/api/webhook/test', 'x=1')
        if hook.wait_for(count + 1):
            h = hook.heads[count][1]
            check('a test goes with a middling priority and no token once forgotten', h.get('Priority') == '3' and 'Authorization' not in h, h)

        # Gotify: JSON with a title, the message and a priority, and the application's token in a header.
        check('Gotify needs a token', api('POST', '/api/webhook', 'format=gotify&alerts=1&url=' + urllib.parse.quote(hook.base + '/message', safe=''))[0] == 400)
        gotify = urllib.parse.quote(hook.base + '/message', safe='')
        check('Gotify is saved', api('POST', '/api/webhook', f'format=gotify&url={gotify}&token=AppToken.1&alerts=1&problems=1')[0] == 200)
        count = len(hook.received)
        get(f'/sensor?{sid(1)}')
        if hook.wait_for(count + 1):
            where, h = hook.heads[count]
            message = json.loads(hook.received[count][1])
            check('an alert goes to Gotify as its message', where == '/message' and set(message) == {'title', 'message', 'priority'}
                  and message['priority'] == 10 and message['title'].endswith(': alert: Front "gate"'), message)
            check("with the application's token", h.get('X-Gotify-Key') == 'AppToken.1', h)
        else:
            check('an alert reaches Gotify', False)
        clear(1)

        # The weekly summary: its day and time, and one sent now.
        check('a weekly day past Saturday is refused', api('POST', '/api/webhook', f'url={hook.url}&alerts=1&weekday=7')[0] == 400)
        check('the weekly summary is saved', api('POST', '/api/webhook', f'url={hook.url}&format=json&alerts=1&problems=1&weekday=1&weekminute=1200')[0] == 200)
        saved = get_json('/api/webhook')
        check('and reads back', saved['weekday'] == 1 and saved['weekminute'] == 1200 and saved['format'] == 'json', saved)
        count = len(hook.received)
        check('one can be sent now', api('POST', '/api/webhook/weekly', 'x=1')[0] == 200)
        weekly = json.loads(hook.received[count][1]) if hook.wait_for(count + 1) else {}
        check('the weekly summary counts the week', weekly.get('event') == 'weekly summary' and weekly.get('alerts', 0) >= 3
              and weekly.get('busiest') and ' weekly summary: ' in weekly.get('message', ''), weekly)
        if time.gmtime().tm_sec > 54:
            time.sleep(7)  # not at the end of a minute, which would be over before the hub looks
        now = time.gmtime()
        day = (now.tm_wday + 1) % 7  # Python counts from Monday, the hub from Sunday
        count = len(hook.received)
        api('POST', '/api/webhook', f'url={hook.url}&alerts=1&problems=1&weekday={day}&weekminute={now.tm_hour * 60 + now.tm_min}')
        check('the weekly summary arrives at its time', hook.wait_for(count + 1) and json.loads(hook.received[count][1]).get('event') == 'weekly summary', hook.received[count:])
        time.sleep(1)  # time for a second one to be wrongly sent
        check('and only once', len(hook.received) == count + 1, hook.received[count + 1:])
        api('POST', '/api/webhook', f'url={hook.url}&alerts=1&problems=1')
    finally:
        api('POST', f'/api/sensor-settings?path={path}', '')
        hook.close()


@case(pc=True)
def trouble_reports(s):
    """The daily check, a sensor firing too often, and nodes going quiet together, with the webhook catching what is sent."""
    hook = HookCatcher()
    try:
        def sent(event):
            found = []
            for _, body in list(hook.received):
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
        check('a daily check time past midnight is refused', api('POST', '/api/webhook', f'url={hook.url}&alerts=1&problems=1&daily=1440')[0] == 400)
        check('the daily check time is saved', api('POST', '/api/webhook', f'url={hook.url}&alerts=1&problems=1&daily={minute}')[0] == 200
              and get_json('/api/webhook')['daily'] == minute)
        daily = wait_until(lambda: sent('daily check'), timeout=5)
        check('the daily check arrives at its time', len(daily) == 1 and daily[0]['nodes'] >= 2 and ' daily check: ' in daily[0]['message']
              and 'nvrListening' in daily[0], daily)
        time.sleep(1)  # time for a second one to be wrongly sent
        check('and only once', len(sent('daily check')) == 1)
        api('POST', '/api/webhook', f'url={hook.url}&alerts=1&problems=1')
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
    finally:
        hook.close()


@case(pc=True)
def what_needs_the_password(s):
    """The webhook settings, the event history and the status."""
    check('the webhook settings need the web password', api('GET', '/api/webhook', auth=False)[0] == 401)
    check('the event history needs the web password', api('GET', '/api/events', auth=False)[0] == 401)
    check('/status needs the web password', http_status('/status') == 401)


@case(pc=True)
def a_reply_too_large_for_memory(s):
    """A reply larger than the memory left fails alone, and the hub keeps running."""
    for _ in range(8):  # a history longer than the 2 KB the hub has left
        get(f'/sensor?{sid(6)}')
        clear(6)
    control('memory-short')
    code = api('GET', '/api/events')[0]
    control('memory-normal')
    check('a reply larger than the memory left fails alone, and the hub keeps running', code == 507 and api('GET', '/api/events')[0] == 200, code)


@case(pc=True)
def no_memory_left_at_all(s):
    """A radio alert still starts, is logged and is confirmed, and the hub keeps running."""
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


@case(pc=True)
def events_list_tag(s):
    """The Events page asks every 3 seconds; an unchanged list is answered without being built again."""
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


@case(pc=True)
def alerts_the_nvr_missed(s):
    """Alerts the NVR did not get, played once it listens again. Nothing subscribes from 192.0.2.8, so with that as the
    NVR's address it is not listening."""
    path = name(1)
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


@case(pc=True)
def a_report_waits_for_a_busy_hub(s):
    """A report that arrives while every web connection is busy waits for one, instead of being turned away."""
    idle = [socket.create_connection((HOST, WEB)) for _ in range(6)]
    time.sleep(0.3)  # time for the hub to take them all
    result = {}
    sender = threading.Thread(target=lambda: result.update(code=get(f'/sensor?{sid(1)}')[0]))
    sender.start()
    time.sleep(1)  # the report waits meanwhile
    idle.pop().close()
    sender.join(15)
    for c in idle:
        c.close()
    check('a report sent while every connection is busy waits, and arrives', result.get('code') == 200, result)
    clear(1)


@case(pc=True)
def a_live_alert_interrupts_a_past_event(s):
    """A live alert never shares its NVR record with a past event: the past event stops and plays again later."""
    get(f'/sensor?{sid(2)}&age=500')
    playing = wait_until(lambda: name(2) in status()['active'], timeout=20)
    get(f'/sensor?{sid(1)}')
    now_on = status()
    check('a live alert stops a past event that was playing', playing and now_on['active'] == [name(1)] and now_on['delayed_waiting'] >= 1, now_on)
    control('end-alerts')
    check('and the past event plays again once the live one is over', wait_until(lambda: name(2) in status()['active'], timeout=20), status())
    control('end-alerts')


@case(pc=True)
def a_firmware_upload_pauses_the_hub(s):
    """During an upload the hub drops video and takes nothing new, so the update has the memory it needs. And the backup."""
    video, code, _ = rtsp_describe()
    video.settimeout(3)
    upload = socket.create_connection((HOST, WEB), timeout=15)
    login = base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()
    image = signed(b'\xe9' + b'\x01' * 19999)
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
