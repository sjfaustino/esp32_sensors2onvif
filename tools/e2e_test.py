"""End-to-end test of the firmware library, built for the PC (pio run -e host).

Plays the NVR's part using the call order recorded from the real NVR. By default every socket is
loopback. Needs ffmpeg.

To test a flashed board instead, restart it first, then set E2E_DEVICE to its address and
E2E_LOCAL_IP to this PC's address on the same network. If the board has a password, set
E2E_WEB_PASSWORD to it and E2E_SENSOR_TOKEN to the token on its HTTP page. It creates and deletes only the 02-00-00-00-00-0x test folders. If the board
has its own clear.h264 or text.json it skips the checks that would replace them, and it stops
if it cannot tell. Never point E2E_DEVICE at an NVR.
"""
import base64, datetime, gzip, hashlib, hmac, http.client, http.server, json, os, re, socket, subprocess, sys, tempfile, threading, time, urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_frames

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, '.pio', 'build', 'host', 'program.exe')
DEVICE = os.environ.get('E2E_DEVICE')
HOST = DEVICE or '127.0.0.1'
LOCAL = os.environ.get('E2E_LOCAL_IP', '127.0.0.1')
ONVIF, RTSP, DISCOVERY = (8899, 8554, 3702) if DEVICE else (18899, 18554, 13702)
WEB = 80 if DEVICE else 18080
HOLD_MS = 10000 if DEVICE else 8000
PASSWORD = os.environ.get('E2E_WEB_PASSWORD', '') if DEVICE else 'test-pass'
TOKEN = os.environ.get('E2E_SENSOR_TOKEN', '')
# A board uses one password for the web page, ONVIF and RTSP. The PC build's ONVIF stays open until a test turns its login on.
BOARD_LOGIN = PASSWORD if DEVICE else ''
A, B = '02-00-00-00-00-01', '02-00-00-00-00-02'
SENSORS = {1: (A, 1), 2: (A, 2), 3: (A, 3), 4: (B, 1), 5: (B, 2), 6: (B, 3)}
failures = []


def check(name, ok, detail=''):
    print(('PASS ' if ok else 'FAIL ') + name + (f'  [{detail}]' if detail != '' and not ok else ''), flush=True)
    if not ok:
        failures.append(name)


def name(n):
    return f'{SENSORS[n][0]}/{SENSORS[n][1]}'


def sid(n):
    return f'mac={SENSORS[n][0]}&input={SENSORS[n][1]}'


def wsse_header(password, digest=True, user='admin'):
    nonce, created = os.urandom(16), '2026-01-01T00:00:00Z'
    value = base64.b64encode(hashlib.sha1(nonce + created.encode() + password.encode()).digest()).decode() if digest else password
    return ('<s:Header><Security xmlns="http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd"><UsernameToken>'
            f'<Username>{user}</Username><Password>{value}</Password><Nonce>{base64.b64encode(nonce).decode()}</Nonce><Created>{created}</Created>'
            '</UsernameToken></Security></s:Header>')


def soap(action, path='/onvif/media', inner=''):
    header = wsse_header(BOARD_LOGIN) if BOARD_LOGIN else ''
    body = (f'<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">{header}<s:Body>'
            f'<{action} xmlns="x">{inner}</{action}></s:Body></s:Envelope>')
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('POST', path, body, {'Content-Type': 'application/soap+xml', 'Host': f'{HOST}:{ONVIF}'})
    r = c.getresponse()
    data = r.read().decode()
    c.close()
    return r.status, data


def body_of(r):
    """The reply's body, unzipped if the hub sent it gzipped, as it does the web pages. Notes how it came."""
    global last_encoding, last_transfer
    last_encoding = r.getheader('Content-Encoding')
    last_transfer = r.getheader('Transfer-Encoding')
    data = r.read()
    return gzip.decompress(data) if last_encoding == 'gzip' else data


last_encoding = last_transfer = None


def get(path):
    headers = {}
    if PASSWORD:
        headers['Authorization'] = 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()
    if TOKEN and path.startswith('/sensor'):
        path += f'&token={TOKEN}'
    c = http.client.HTTPConnection(HOST, WEB, timeout=15)  # a busy hub leaves it waiting, up to its 10 s stall limit
    c.request('GET', path, headers=headers)
    r = c.getresponse()
    data = body_of(r).decode()
    c.close()
    return r.status, data


def http_status(path):
    c = http.client.HTTPConnection(HOST, WEB, timeout=5)
    c.request('GET', path)
    r = c.getresponse()
    r.read()
    c.close()
    return r.status


def soap_login(action, password, digest=True, path='/onvif/media', user='admin'):
    body = f'<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">{wsse_header(password, digest, user)}<s:Body><{action} xmlns="x"/></s:Body></s:Envelope>'
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('POST', path, body, {'Content-Type': 'application/soap+xml'})
    r = c.getresponse()
    data = r.read().decode()
    c.close()
    return r.status, data


def rtsp_describe(authorization=None):
    """Returns (status code, WWW-Authenticate lines)."""
    s = socket.create_connection((HOST, RTSP), timeout=5)
    extra = f'Authorization: {authorization}\r\n' if authorization else ''
    s.sendall(f'DESCRIBE rtsp://{HOST}:{RTSP}/stream RTSP/1.0\r\nCSeq: 1\r\n{extra}\r\n'.encode())
    data = s.recv(4096).decode(errors='replace')
    return s, int(data.split(' ')[1]), [l for l in data.split('\r\n') if l.startswith('WWW-Authenticate')]


def api(method, path, body=None, auth=True, origin=None):
    """Returns (status, body bytes)."""
    headers = {'Origin': origin} if origin else {}
    if auth and PASSWORD:
        headers['Authorization'] = 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()
    c = http.client.HTTPConnection(HOST, WEB, timeout=15)
    c.request(method, path, body, headers)
    r = c.getresponse()
    data = body_of(r)
    c.close()
    return r.status, data


def raw_get(path, host=None, source=None, password=None):
    """A GET with a chosen Host header, from a chosen local address, with a chosen password. Returns the status."""
    c = http.client.HTTPConnection(HOST, WEB, timeout=5, source_address=(source, 0) if source else None)
    c.putrequest('GET', path, skip_host=host is not None)
    if host is not None:
        c.putheader('Host', host)
    if password is not None:
        c.putheader('Authorization', 'Basic ' + base64.b64encode(f'admin:{password}'.encode()).decode())
    c.endheaders()
    r = c.getresponse()
    r.read()
    c.close()
    return r.status


def status():
    return json.loads(get('/status')[1])


HUB = None  # the PC build's process, while it runs


def wait_until(condition, timeout=10, step=0.05):
    """Polls condition until it is true or the time is up, and returns its last value. For waiting on the hub;
    a fixed sleep is used only where the time itself is being tested, or to give the hub a chance to do something wrong."""
    end = time.time() + timeout
    while True:
        value = condition()
        if value or time.time() >= end:
            return value
        time.sleep(step)


def poll(fetch, test, timeout=10, step=0.05):
    """Fetches a value until test(value) is true or the time is up, and returns the last value fetched."""
    end = time.time() + timeout
    while True:
        value = fetch()
        if test(value) or time.time() >= end:
            return value
        time.sleep(step)


def event_list():
    return json.loads(api('GET', '/api/events')[1])


def live():
    return json.loads(api('GET', '/api/live')[1])


def control(command):
    """Sends one of the PC build's test commands and returns once it has taken effect, with its answer if it has one."""
    HUB.stdin.write(command + '\n')
    HUB.stdin.flush()
    while True:
        line = HUB.stdout.readline()
        if not line:
            sys.exit(f'the PC build stopped while running {command!r}')
        line = line.strip()
        if line == f'unknown {command}':
            sys.exit(f'the PC build has no command {command!r}')
        if line == f'done {command}' or line.startswith(f'done {command}: ') or line == f'done {command}:':
            return line[len(f'done {command}:'):].strip()


def start_hub(*args):
    global HUB
    HUB = subprocess.Popen([EXE, str(HOLD_MS), *args], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    return HUB


def clear(n):
    """Reports sensor n clear and ends its alert. A clear report alone does not, so the PC build has a command for it and a board waits out the hold."""
    get(f'/sensor?{sid(n)}&state=clear')
    if DEVICE:
        time.sleep(HOLD_MS / 1000 + 0.5)
    else:
        control('end-alerts')


class PullingNvr:
    """An NVR that pulls events: subscribes, then asks for events every few seconds, as a real one does. The hub counts
    it as listening only while it keeps asking, so stop() is how a test takes it away."""

    def __init__(self):
        reply = soap('CreatePullPointSubscription', '/onvif/events')[1]
        self.address = re.search(r'<wsa:Address>http://[^/]+([^<]+)</wsa:Address>', reply).group(1)
        self.stopping = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stopping.is_set():
            try:
                soap('PullMessages', self.address)
            except OSError:
                pass  # the hub restarting under a test
            self.stopping.wait(20)  # well inside the minute the hub allows, and seldom in the way of the other checks

    def stop(self):
        self.stopping.set()
        self.thread.join()


class FakeNvrListener:
    """Records each Notify and, like the real NVR, closes without an HTTP reply."""

    def __init__(self):
        self.sock = socket.socket()
        self.sock.bind((LOCAL, 0))
        self.sock.listen(16)
        self.port = self.sock.getsockname()[1]
        self.events = []
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        while True:
            conn, _ = self.sock.accept()
            conn.settimeout(2)
            data = b''
            try:
                while b'</s:Envelope>' not in data:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    data += chunk
            except socket.timeout:
                pass
            conn.close()
            self.events.append((time.time(), data.decode(errors='replace')))

    def states(self):
        return [re.search(r'Name="State" Value="(\w+)"', e[1]).group(1) for e in self.events]


def grab_colour():
    """Pull one decoded frame over RTSP/TCP and report which colour dominates."""
    login = f'admin:{urllib.parse.quote(BOARD_LOGIN, safe="")}@' if BOARD_LOGIN else ''
    out = subprocess.run(['ffmpeg', '-v', 'error', '-rtsp_transport', 'tcp', '-i', f'rtsp://{login}{HOST}:{RTSP}/stream', '-frames:v', '1',
                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], capture_output=True, timeout=40)
    if len(out.stdout) != 640 * 360 * 3:
        error = out.stderr.decode(errors='replace')
        return 'no frame: ' + (error.replace(login, 'LOGIN@') if login else error)[:300]  # ffmpeg repeats the address, password and all
    r, g = sum(out.stdout[0::3]), sum(out.stdout[1::3])
    if g > r:
        return 'green'
    return 'amber' if g > 0.3 * r else 'red'


def dropped_after(port, data):
    """True when the hub hangs up on a request it should not keep buffering."""
    s = socket.create_connection((HOST, port), timeout=5)
    try:
        s.sendall(data)
        return s.recv(16) == b''
    except ConnectionError:
        return True
    except socket.timeout:
        return False
    finally:
        s.close()


class FakeBroker:
    """A pretend MQTT broker: records the hub's login and every message it publishes, and acknowledges them. It can be
    told to refuse the login, or to close the connection on the next event instead of acknowledging it."""

    def __init__(self, host):
        self.server = socket.socket()
        self.server.bind((host, 0))
        self.server.listen(2)
        self.port = self.server.getsockname()[1]
        self.connects, self.messages, self.subscriptions = [], [], []
        self.conn = None  # the hub's connection, for sending it a command
        self.refuse = 0  # the CONNACK return code; 0 accepts
        self.drop_next_event = False
        threading.Thread(target=self._accept, daemon=True).start()

    def close(self):
        self.server.close()

    def _accept(self):
        while True:
            try:
                c, _ = self.server.accept()
            except OSError:
                return
            self.conn = c
            threading.Thread(target=self._serve, args=(c,), daemon=True).start()

    @staticmethod
    def _size(buf):
        """The whole packet's size, or 0 while it is not all there."""
        length, scale = 0, 1
        for i in range(1, 5):
            if i >= len(buf):
                return 0
            length += (buf[i] & 0x7F) * scale
            if not buf[i] & 0x80:
                return 1 + i + length if len(buf) >= 1 + i + length else 0
            scale *= 128
        raise ValueError('bad length')

    def _serve(self, c):
        buf = b''
        try:
            while True:
                data = c.recv(4096)
                if not data:
                    break
                buf += data
                while (size := self._size(buf)):
                    packet, buf = buf[:size], buf[size:]
                    if not self._handle(c, packet):
                        c.close()
                        return
        except OSError:
            pass

    def _handle(self, c, p):
        kind, at = p[0] >> 4, 1
        while p[at] & 0x80:
            at += 1
        body = p[at + 1:]

        def text(pos):
            n = body[pos] << 8 | body[pos + 1]
            return body[pos + 2:pos + 2 + n], pos + 2 + n

        if kind == 1:  # CONNECT
            flags, pos = body[7], 10
            client, pos = text(pos)
            hello = {'client': client.decode(), 'clean': bool(flags & 2), 'will_retain': bool(flags & 0x20), 'will_qos': flags >> 3 & 3}
            if flags & 4:
                topic, pos = text(pos)
                message, pos = text(pos)
                hello['will'] = (topic.decode(), message)
            if flags & 0x80:
                user, pos = text(pos)
                hello['user'] = user.decode()
            if flags & 0x40:
                password, pos = text(pos)
                hello['password'] = password.decode()
            self.connects.append(hello)
            c.sendall(bytes([0x20, 2, 0, self.refuse]))
            return self.refuse == 0
        if kind == 3:  # PUBLISH
            qos, retain = p[0] >> 1 & 3, bool(p[0] & 1)
            topic, pos = text(0)
            pid = body[pos] << 8 | body[pos + 1] if qos else 0
            payload = body[pos + 2:] if qos else body[pos:]
            self.messages.append({'topic': topic.decode(), 'payload': payload, 'retain': retain, 'qos': qos})
            if qos:
                if self.drop_next_event:
                    self.drop_next_event = False
                    return False
                c.sendall(bytes([0x40, 2, pid >> 8, pid & 0xFF]))
        if kind == 8:  # SUBSCRIBE
            pid = body[0] << 8 | body[1]
            topic, pos = text(2)
            self.subscriptions.append((topic.decode(), body[pos]))
            c.sendall(bytes([0x90, 3, pid >> 8, pid & 0xFF, 0]))
        if kind == 12:  # PINGREQ
            c.sendall(b'\xd0\x00')
        return True

    def send(self, topic, payload):
        """Publishes to the hub at QoS 0, as Home Assistant's switch does."""
        t = topic.encode()
        body = bytes([len(t) >> 8, len(t) & 0xFF]) + t + payload
        self.conn.sendall(bytes([0x30, len(body)]) + body)

    def latest(self, topic):
        found = [m for m in self.messages if m['topic'] == topic]
        return found[-1] if found else None

    def state(self, topic):
        m = self.latest(topic)
        return json.loads(m['payload']) if m and m['payload'] else None

    def events(self, topic):
        return [m for m in self.messages if m['topic'] == topic and m['qos'] == 1]


def mqtt_tests(sensor_alert, sensor_clear):
    """Home Assistant through MQTT, against a pretend broker on this PC."""
    settings = json.loads(api('GET', '/api/mqtt')[1])
    if settings['host']:
        print('SKIP MQTT: the hub already has a broker set, which these tests would replace')
        return
    broker = FakeBroker(LOCAL)
    serial = json.loads(api('GET', '/api/info')[1])['serial']
    base = f'sensors2onvif/{serial}'

    def key(n):
        return SENSORS[n][0].replace('-', '').lower() + f'_{SENSORS[n][1]}'

    def mqtt_page():
        return json.loads(api('GET', '/api/mqtt')[1])

    check('a broker given by name is refused', api('POST', '/api/mqtt', 'host=broker.lan&port=1883')[0] == 400)
    check('a broker port out of range is refused', api('POST', '/api/mqtt', f'host={LOCAL}&port=70000')[0] == 400)
    saved = api('POST', '/api/mqtt', f'host={LOCAL}&port={broker.port}&user=hub&password=' + urllib.parse.quote('s3cret pw'))[0]
    check('the MQTT settings can be saved', saved == 200)
    page = poll(mqtt_page, lambda m: m['connected'])
    check('the hub connects to the broker', page['connected'] and page['state'] == 'connected', page)
    check('the page never shows the MQTT password', page['password'] is True and 's3cret' not in json.dumps(page), page)
    hello = broker.connects[0] if broker.connects else {}
    check('the hub logs in with its user and password', hello.get('user') == 'hub' and hello.get('password') == 's3cret pw', hello)
    check('the hub leaves "offline" with the broker as its retained last will', hello.get('will') == (f'{base}/status', b'offline') and hello.get('will_retain'), hello)
    online = wait_until(lambda: broker.latest(f'{base}/status'))
    check('the hub says it is online, retained', online and online['payload'] == b'online' and online['retain'], online)

    # Home Assistant's discovery.
    topic = f'homeassistant/binary_sensor/{serial}/{key(1)}_alert/config'
    told = wait_until(lambda: broker.latest(topic))
    config = json.loads(told['payload']) if told else {}
    check('Home Assistant is told of a sensor, retained',
          told and told['retain'] and config.get('state_topic') == f'{base}/{key(1)}/state' and config.get('availability_topic') == f'{base}/status'
          and config.get('device', {}).get('via_device') == f'sensors2onvif_{serial}', config)
    check('and of every sensor', wait_until(lambda: all(broker.latest(f'homeassistant/event/{serial}/{key(n)}_events/config') for n in SENSORS)))
    hub = wait_until(lambda: broker.latest(f'homeassistant/binary_sensor/{serial}/hub_alert/config'))
    check('and of the hub', hub and json.loads(hub['payload'])['device']['identifiers'] == [f'sensors2onvif_{serial}'], hub)

    # An alert: the sensor's state at once, and the event, which the broker acknowledges.
    sensor_alert(1)
    on = wait_until(lambda: (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'ON')
    check('an alert turns the sensor on in Home Assistant', on, broker.state(f'{base}/{key(1)}/state'))
    check('and the hub', (broker.state(f'{base}/hub/state') or {}).get('alert') == 'ON', broker.state(f'{base}/hub/state'))
    sent = wait_until(lambda: [e for e in broker.events(f'{base}/{key(1)}/event') if json.loads(e['payload'])['event_type'] == 'alert'])
    check('the alert is sent as an event the broker must acknowledge', sent and not sent[-1]['retain'], sent)
    page = poll(mqtt_page, lambda m: m['waiting'] == 0)
    check('the event counts as sent once acknowledged', page['waiting'] == 0, page)
    sensor_clear(1)
    off = wait_until(lambda: (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'OFF', timeout=15)
    check('the alert ending turns the sensor off', off, broker.state(f'{base}/{key(1)}/state'))

    # An event the broker never acknowledged is sent again once the hub has reconnected.
    before = len(broker.events(f'{base}/{key(1)}/event'))
    broker.drop_next_event = True
    sensor_alert(1)
    again = wait_until(lambda: len(broker.events(f'{base}/{key(1)}/event')) >= before + 2, timeout=30)
    events = broker.events(f'{base}/{key(1)}/event')
    check('an event lost with the connection is sent again after reconnecting', again and events[-1]['payload'] == events[-2]['payload'], events[before:])
    check('and the hub logged in again', len(broker.connects) >= 2)
    sensor_clear(1)

    # A sensor added appears in Home Assistant, and one deleted is taken away again.
    path = '02-00-00-00-00-98/1'
    gone_topic = f'homeassistant/binary_sensor/{serial}/020000000098_1_alert/config'
    api('POST', f'/api/mkdir?path={path}')
    added = wait_until(lambda: (broker.latest(gone_topic) or {}).get('payload'))
    check('a sensor added appears in Home Assistant', added)
    api('PUT', f'/api/file?path={path}/name.txt', b'Back door')
    renamed = wait_until(lambda: json.loads(broker.latest(gone_topic)['payload'] or b'{}').get('device', {}).get('name') == 'Back door')
    check('a sensor renamed is renamed in Home Assistant', renamed, broker.latest(gone_topic))
    told = len(broker.messages)
    api('POST', f'/api/sensor-settings?path={path}', 'hold=20')
    time.sleep(0.5)  # time for discovery to be wrongly sent again
    check('a change Home Assistant does not show sends nothing again', len(broker.messages) == told, broker.messages[told:][:3])
    api('DELETE', '/api/file?path=02-00-00-00-00-98')
    removed = wait_until(lambda: broker.latest(gone_topic)['payload'] == b'' and (broker.latest(f'{base}/020000000098_1/state') or {}).get('payload') == b'')
    check('a sensor deleted is taken away from Home Assistant, with its retained state', removed, broker.latest(gone_topic))

    # The Armed switch.
    switch = wait_until(lambda: broker.latest(f'homeassistant/switch/{serial}/hub_armed/config'))
    switch = json.loads(switch['payload']) if switch else {}
    check('Home Assistant gets an Armed switch', switch.get('command_topic') == f'{base}/armed/set' and switch.get('state_topic') == f'{base}/hub/state', switch)
    check('the hub listens for the switch', (f'{base}/armed/set', 0) in broker.subscriptions, broker.subscriptions)
    check('the switch shows the hub armed', (broker.state(f'{base}/hub/state') or {}).get('armed') == 'ON', broker.state(f'{base}/hub/state'))
    broker.send(f'{base}/armed/set', b'OFF')
    off = poll(lambda: json.loads(api('GET', '/api/arm')[1]), lambda a: a['armed'] is False)
    check('Home Assistant can disarm the hub', off['armed'] is False, off)
    newest = event_list()[0]
    check('disarming is logged, saying from where', newest['sensor'] == 'hub' and newest['event'] == 'disarmed' and newest['detail'] == 'Home Assistant', newest)
    check('the switch shows the hub disarmed', wait_until(lambda: (broker.state(f'{base}/hub/state') or {}).get('armed') == 'OFF'))
    sensor_alert(1)
    held = wait_until(lambda: [e for e in broker.events(f'{base}/{key(1)}/event') if json.loads(e['payload'])['event_type'] == 'alert while disarmed'])
    check('an alert while disarmed reaches Home Assistant as that event', held)
    check('and the sensor does not turn on', (broker.state(f'{base}/{key(1)}/state') or {}).get('alert') == 'OFF', broker.state(f'{base}/{key(1)}/state'))
    sensor_clear(1)
    broker.send(f'{base}/armed/set', b'ON')
    on = poll(lambda: json.loads(api('GET', '/api/arm')[1]), lambda a: a['armed'] is True)
    check('Home Assistant can arm the hub', on['armed'] is True, on)

    # A broker that refuses the login says why on the page.
    broker.refuse = 5
    api('POST', '/api/mqtt', f'host={LOCAL}&port={broker.port}&user=hub&password=')
    refused = poll(mqtt_page, lambda m: 'refused' in m['state'])
    check('a refused login shows on the page', refused['state'] == 'the broker refused: not authorized', refused)
    check('a password left blank keeps the saved one', broker.connects[-1].get('password') == 's3cret pw', broker.connects[-1])
    broker.refuse = 0

    check('the MQTT settings need the web password', api('GET', '/api/mqtt', auth=False)[0] == 401)
    check('MQTT can be turned off', api('POST', '/api/mqtt', 'host=')[0] == 200 and poll(mqtt_page, lambda m: m['state'] == 'not set up')['state'] == 'not set up')
    broker.close()


def arm_tests():
    """Disarmed, alerts are logged but not sent to the NVR; one that happened before the disarm still is."""
    check('arming needs the web password', api('POST', '/api/arm', 'armed=0', auth=False)[0] == 401)
    check('a bad arm value is refused', api('POST', '/api/arm', 'armed=maybe')[0] == 400)
    check('the hub can be disarmed', api('POST', '/api/arm', 'armed=0')[0] == 200 and json.loads(api('GET', '/api/arm')[1])['armed'] is False)
    newest = event_list()[0]
    check('disarming from the page is logged', newest['event'] == 'disarmed' and newest['detail'] == 'the web page', newest)
    check('/status says the hub is disarmed', status()['armed'] is False)
    get(f'/sensor?{sid(1)}')
    newest = event_list()[0]
    check('an alert while disarmed is logged as that', newest['sensor'] == name(1) and newest['kind'] == 10 and newest['event'] == 'alert while disarmed', newest)
    check('and is not sent to the NVR', name(1) not in status()['active'], status())
    clear(1)
    get(f'/sensor?{sid(2)}&age=500')  # it happened before the hub was disarmed, a moment ago
    check('an alert from before the disarm still reaches the NVR', wait_until(lambda: name(2) in status()['active'], timeout=20), status())
    clear(2)
    get(f'/sensor?{sid(3)}&age=unknown')
    check('so does one whose time is not known', wait_until(lambda: name(3) in status()['active'], timeout=20), status())
    clear(3)
    check('the hub can be armed again', api('POST', '/api/arm', 'armed=1')[0] == 200 and status()['armed'] is True)
    get(f'/sensor?{sid(1)}')
    check('armed, an alert reaches the NVR again', name(1) in status()['active'], status())
    clear(1)


def file_manager_tests(files, hub_files_free):
    if PASSWORD:
        check('file manager needs the password', api('GET', '/', auth=False)[0] == 401)
        check('file operations need the password', api('DELETE', f'/api/file?path={A}', auth=False)[0] == 401)
        check('picture upload needs the password', api('PUT', '/api/file?path=clear.h264', b'x' * 2000, auth=False)[0] == 401)
    for node in (A, B):
        api('DELETE', f'/api/file?path={node}')
    baseline = status()['sensors']

    check('endless web headers are dropped without a login', dropped_after(WEB, b'GET / HTTP/1.1\r\nX: ' + b'a' * 8000))
    check('endless ONVIF headers are dropped without a login', dropped_after(ONVIF, b'POST / HTTP/1.1\r\nX: ' + b'a' * 8000))
    check('endless RTSP headers are dropped without a login', dropped_after(RTSP, b'OPTIONS * RTSP/1.0\r\nX: ' + b'a' * 8000))
    check('an ONVIF request over 8 KB is dropped before it is held', dropped_after(ONVIF, b'POST /onvif/device_service HTTP/1.1\r\nContent-Length: 8193\r\n\r\n<'))
    own_site = f'http://{HOST}' + ('' if WEB == 80 else f':{WEB}')
    check('another site cannot use a logged-in browser', api('POST', '/api/restore', b'hello', origin='http://evil.example')[0] == 403)
    check('the hub\'s own page is not refused as another site', api('POST', '/api/restore', b'hello', origin=own_site)[0] == 400)

    code, page = api('GET', '/')
    check('page is served', code == 200 and b'SensorCam files' in page, code)
    check('page is sent gzipped', last_encoding == 'gzip', last_encoding)
    if not DEVICE:
        # Another site pointing its own name at the hub is refused; the hub's own names are not.
        check('a page under another site\'s name is refused', raw_get('/api/info', host='hub.attacker.example', password=PASSWORD) == 421)
        check('a look-alike name is refused', raw_get('/api/info', host='hub.local.attacker.example', password=PASSWORD) == 421)
        check('a sensor report under any name is never refused for its name', raw_get('/sensor?mac=02-00-00-00-00-77', host='sensor.made.up.name') == 404)
        for own in ('127.0.0.1:18080', 'hub.local', 'HUB.local:18080', 'hub.lan', 'hub'):
            check(f'the hub\'s own name {own} is served', raw_get('/api/info', host=own, password=PASSWORD) == 200)
        # Ten wrong passwords from one address shut it out, even with the right one, and only that address.
        for _ in range(10):
            raw_get('/api/info', source='127.0.0.2', password='wrong')
        check('ten wrong passwords shut that address out', raw_get('/api/info', source='127.0.0.2', password=PASSWORD) == 401)
        check('other addresses are not shut out', raw_get('/api/info', password=PASSWORD) == 200)
    check('page has its stylesheet and script inside it', b'<style>' in page and b'const STATES = ' in page and b'src="index.js"' not in page)
    check('page offers a picture preview', b'title: "Preview"' in page)
    check('page has the menu',all(t in page for t in [b'Network Manager', b'Sensors Manager', b'data-view="network"', b'data-view="wifi"', b'data-view="onvif"', b'data-view="http"', b'data-view="rtsp"', b'data-view="sensors"',
                                                  b'data-view="espnow"', b'data-view="events"', b'System Manager', b'data-view="maintenance"', b'data-view="time"', b'data-view="device"', b'NVR Manager', b'data-view="nvr"', b'data-view="live">Live Video<']))
    code, info = api('GET', '/api/info')
    info = json.loads(info) if code == 200 else {}
    check('hub details report its ports', info.get('onvifPort') == ONVIF and info.get('webPort') == WEB and info.get('rtspPort') == RTSP, info)
    check('hub details need the web password', not PASSWORD or api('GET', '/api/info', auth=False)[0] == 401)
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('GET', '/')
    r = c.getresponse()
    r.read()
    c.close()
    check('ONVIF port does not serve the web page', r.status == 404, r.status)
    c = http.client.HTTPConnection(HOST, WEB, timeout=5)
    c.request('POST', '/onvif/device_service', '<x/>', {'Content-Type': 'application/soap+xml'})
    r = c.getresponse()
    r.read()
    c.close()
    check('web port does not answer ONVIF', r.status == 404, r.status)

    for action, wanted in (('GetNetworkInterfaces', 'HwAddress'), ('GetHostname', 'HostnameInformation'), ('GetDNS', 'DNSInformation'), ('GetNTP', 'NTPInformation'),
                           ('GetNetworkDefaultGateway', 'NetworkGateway'), ('GetNetworkProtocols', 'NetworkProtocols'), ('GetDiscoveryMode', 'Discoverable'), ('GetUsers', 'UserLevel')):
        code, data = soap(action, '/onvif/device_service')
        check(f'ONVIF answers {action}', code == 200 and wanted in data, data[:200])
    check('an unsupported ONVIF request is refused', soap('NoSuchRequest', '/onvif/device_service')[0] == 400)
    check('an unsupported ONVIF request is recorded', 'NoSuchRequest' in json.loads(api('GET', '/api/info')[1])['unsupported'])
    check('setting the time over ONVIF is refused, because the hub would ignore it', soap('SetSystemDateAndTime', '/onvif/device_service')[0] == 400)

    code, device = api('GET', '/api/device')
    rows = dict(json.loads(device)['rows']) if code == 200 else {}
    check('device info is served', 'Firmware version' in rows and 'Picture storage' in rows and 'Sensors' in rows, rows)
    check('time settings are served', api('GET', '/api/time')[0] == 200 and 'local' in json.loads(api('GET', '/api/time')[1]))
    code, sched = api('GET', '/api/schedule')
    check('restart schedule is served', code == 200 and 'minute' in json.loads(sched), code)
    check('device info needs the web password', not PASSWORD or api('GET', '/api/device', auth=False)[0] == 401)

    if not DEVICE:  # the PC build has a pretend crash report; a board's real one is not deleted
        check('device info says a crash report is saved', json.loads(device).get('crash') == 3000, json.loads(device).get('crash'))
        code, dump = api('GET', '/api/crash')
        check('the crash report downloads whole, read in pieces', code == 200 and dump == bytes(i % 251 for i in range(3000)), (code, len(dump)))
        check('the crash report needs the web password', not PASSWORD or api('GET', '/api/crash', auth=False)[0] == 401)
        check('the crash report can be deleted', api('POST', '/api/crash/delete', 'x=1')[0] == 200 and json.loads(api('GET', '/api/device')[1]).get('crash') == 0)
        check('no crash report to download after deleting', api('GET', '/api/crash')[0] == 404)

    if not DEVICE:  # saving Wi-Fi on a real board would move it to another network
        zone = 'offset=60&dst=1'
        for good in (f'mode=ntp&server=pool.ntp.org&{zone}', f'mode=manual&epoch=1790000000&{zone}', f'mode=manual&{zone}', 'mode=ntp&server=time.local&offset=345&dst=0', 'mode=ntp&server=a.b&offset=-720&dst=2'):
            check(f'time is accepted: {good[:34]}', api('POST', '/api/time', good)[0] == 200)
        for bad in (f'mode=ntp&server=&{zone}', f'mode=ntp&server=bad%20name&{zone}', f'mode=manual&epoch=100&{zone}', f'mode=manual&epoch=abc&{zone}', 'mode=ntp&server=a.b&offset=61&dst=0',
                    'mode=ntp&server=a.b&offset=900&dst=0', 'mode=ntp&server=a.b&offset=0&dst=3', 'mode=ntp&server=a.b&dst=0'):
            check(f'time is refused: {bad[:34]}', api('POST', '/api/time', bad)[0] == 400)
        check('time settings need the web password', api('POST', '/api/time', f'mode=ntp&server=a.b&{zone}', auth=False)[0] == 401)
        check('restart schedule is accepted', api('POST', '/api/schedule', 'on=1&day=3&minute=240')[0] == 200)
        saved = json.loads(api('GET', '/api/schedule')[1])
        check('restart schedule reads back', saved == {'on': True, 'day': 3, 'minute': 240}, saved)
        for bad in ('on=1&day=7&minute=0', 'on=1&day=-2&minute=0', 'on=1&day=0&minute=1440', 'on=1&day=0&minute=x', 'on=1'):
            check(f'restart schedule is refused: {bad}', api('POST', '/api/schedule', bad)[0] == 400)
        check('restart needs the web password', api('POST', '/api/reboot', '', auth=False)[0] == 401)
        check('restart request is accepted', api('POST', '/api/reboot', '')[0] == 200)
        check('reset must be confirmed', api('POST', '/api/reset', '')[0] == 400)
        check('reset needs the web password', api('POST', '/api/reset', 'confirm=yes', auth=False)[0] == 401)
        check('confirmed reset is accepted', api('POST', '/api/reset', 'confirm=yes')[0] == 200)

        code, page = api('GET', '/setup', auth=False)
        check('setup page needs the web password on the home network', code == 401, code)
        code, page = api('GET', '/setup')
        check('setup page is served', code == 200 and b'Hub Wi-Fi setup' in page and b'async function scan' in page, code)
        check('network list is served', b'test-net' in api('GET', '/api/wifi/scan')[1])
        if not DEVICE:
            names = [n['ssid'] for n in json.loads(api('GET', '/api/wifi/scan')[1])['networks']]
            check('a network name with quotes, a backslash and control characters arrives exactly', names == ['test-net', 'say "hi" \\ to\tall\x01'], names)
        check('short Wi-Fi password is refused', api('POST', '/api/wifi', 'ssid=x&password=short')[0] == 400)
        check('empty Wi-Fi name is refused', api('POST', '/api/wifi', 'ssid=&password=longenough')[0] == 400)
        secret = 'password=p%25ss%20w%2Brd%26ok'
        check('backup network is saved as the backup', api('POST', '/api/wifi', f'ssid=want-backup&role=backup&{secret}')[0] == 200)
        check('main network is not saved as the backup', api('POST', '/api/wifi', f'ssid=want-backup&role=main&{secret}')[0] == 507)
        check('Wi-Fi password is decoded exactly', api('POST', '/api/wifi', f'ssid=test-net&{secret}')[0] == 200)
        control('setup-on')
        code, page = api('GET', '/', auth=False)
        check('setup network opens the setup page without a login', code == 200 and b'Hub Wi-Fi setup' in page, code)
        c = http.client.HTTPConnection(HOST, 18081, timeout=5)
        c.request('GET', '/generate_204', headers={'Host': 'connectivitycheck.gstatic.com'})
        r = c.getresponse()
        r.read()
        c.close()
        check('phone check reaches setup when the web port has moved', r.status == 302 and r.getheader('Location') == f'http://{HOST}:{WEB}/setup', r.status)
        check('setup network does not expose the files', api('GET', '/api/tree', auth=False)[0] == 401)
        c = http.client.HTTPConnection(HOST, WEB, timeout=5)
        c.request('GET', '/generate_204', headers={'Host': 'connectivitycheck.gstatic.com'})
        r = c.getresponse()
        r.read()
        c.close()
        check('phone connectivity check is sent to the setup page', r.status == 302 and r.getheader('Location') == f'http://{HOST}:{WEB}/setup', r.status)
        check('ONVIF still answers in setup mode', soap('GetDeviceInformation', '/onvif/device_service')[0] == 200)
        control('setup-off')
        check('file manager is back after setup', b'SensorCam files' in api('GET', '/')[1])
        try:
            c = http.client.HTTPConnection(HOST, 18081, timeout=5)
            c.request('GET', '/')
            closed = c.getresponse().status is None
        except (http.client.HTTPException, ConnectionError, socket.timeout):
            closed = True
        finally:
            c.close()
        check('setup port answers nothing outside setup mode', closed)

        check('NVR address needs the web password', api('POST', '/api/nvr', 'ip=192.0.2.7', auth=False)[0] == 401)
        check('NVR address is saved', api('POST', '/api/nvr', 'ip=192.0.2.7')[0] == 200)
        nvr_info = json.loads(api('GET', '/api/nvr')[1])
        check('NVR address reads back', nvr_info['ip'] == '192.0.2.7', nvr_info)
        check('NVR not connected is reported', nvr_info['video'] is False and nvr_info['events'] is False, nvr_info)
        for bad in ('ip=1.2.3', 'ip=999.1.1.1', 'ip=abc'):
            check(f'NVR address is refused: {bad}', api('POST', '/api/nvr', bad)[0] == 400)
        check('NVR address can be cleared', api('POST', '/api/nvr', 'ip=')[0] == 200 and json.loads(api('GET', '/api/nvr')[1])['ip'] == '')
        code, net = api('GET', '/api/network')
        check('network details are served', code == 200 and b'"mac"' in net and b'"gateway"' in net, code)
        check('network settings need the web password', api('POST', '/api/network', 'mode=dhcp', auth=False)[0] == 401)
        fixed = 'mode=static&ip=192.168.1.50&mask=255.255.255.0&gateway=192.168.1.1&dns=8.8.8.8'
        for good in ('mode=dhcp', fixed, 'mode=static&ip=10.1.2.3&mask=255.0.0.0&gateway=10.0.0.1&dns='):
            check(f'address is accepted: {good[:40]}', api('POST', '/api/network', good)[0] == 200)
        for bad in ('mode=static', 'mode=static&ip=999.1.1.1&mask=255.255.255.0&gateway=192.168.1.1',
                    'mode=static&ip=192.168.1.50&mask=255.0.255.0&gateway=192.168.1.1',
                    'mode=static&ip=192.168.1.50&mask=255.255.255.0&gateway=192.168.2.1',
                    'mode=static&ip=192.168.1.0&mask=255.255.255.0&gateway=192.168.1.1',
                    'mode=static&ip=192.168.1.255&mask=255.255.255.0&gateway=192.168.1.1',
                    'mode=static&ip=192.168.1.1&mask=255.255.255.0&gateway=192.168.1.1',
                    'mode=static&ip=192.168.1.50&mask=255.255.255.0&gateway=192.168.1.1&dns=nope',
                    'mode=static&ip=1.2.3&mask=255.255.255.0&gateway=1.2.3.1'):
            check(f'address is refused: {bad[12:50]}', api('POST', '/api/network', bad)[0] == 400)
        check('settings need the web password',api('POST', '/api/settings', 'web=8080', auth=False)[0] == 401)
        for good in ('onvif=9000', 'rtsp=9554', 'name=my-hub-2', 'protect=on&password=', 'protect=on&password=new%20pass', 'protect=off'):
            check(f'setting is accepted: {good}', api('POST', '/api/settings', good)[0] == 200)
        too_long = 'x' * 64
        for bad in ('web=0', 'web=70000', 'web=abc', 'onvif=80x', f'onvif={RTSP}', f'web={ONVIF}', f'onvif={WEB}', 'rtsp=0', 'rtsp=abc', f'rtsp={WEB}', f'rtsp={ONVIF}',
                    'name=-hub', 'name=hub-', 'name=a_b', 'name=a%20b', f'name={too_long}', f'protect=on&password={too_long}'):
            check(f'setting is refused: {bad[:30]}', api('POST', '/api/settings', bad)[0] == 400)

    check('node folder can be added', api('POST', f'/api/mkdir?path={A}')[0] == 200)
    check('node alone is not a sensor', status()['sensors'] == baseline)
    for n in SENSORS:
        check(f'sensor folder {name(n)} can be added', api('POST', f'/api/mkdir?path={name(n)}')[0] == 200)
    check('new folders become sensors without a reboot', status()['sensors'] == baseline + 6, status())

    good = files['approaching.h264']
    path = f'{name(4)}/approaching.h264'
    check('valid picture is accepted', api('PUT', f'/api/file?path={path}', good)[0] == 200)
    if not DEVICE:
        control('disk-nearly-full')
        check('a picture that would take the event history\'s room is refused', api('PUT', f'/api/file?path={path}', good)[0] == 507)
        check('the Sensors page says how much is kept for the history', json.loads(api('GET', '/api/tree')[1])['kept'] > 512 * 1024 - 1)
        control('disk-normal')
    code, back = api('GET', f'/api/file?path={path}')
    check('picture downloads unchanged', code == 200 and back == good, code)

    check('garbage is refused', api('PUT', f'/api/file?path={path}', b'not video at all' * 40)[0] == 422)
    wrong = bytearray(good)
    wrong[8] ^= 0xFF  # inside the SPS, so the format no longer matches
    check('wrong video format is refused', api('PUT', f'/api/file?path={path}', bytes(wrong))[0] == 422)
    # The header a real browser's ffmpeg.wasm produced: same format, different optional details.
    browser_sps = bytes.fromhex('6742c01ed900a02ff961000003000100000300020f162e48')
    from_browser = good.replace(make_frames.nals(good)[0], browser_sps, 1)
    check('browser encoder header is accepted', from_browser != good and api('PUT', f'/api/file?path={path}', from_browser)[0] == 200)
    check('stored picture carries the hub header', api('GET', f'/api/file?path={path}')[1] == good)
    # A browser shows a broken connection as a bare "failed to fetch", so a refusal has to arrive as a reply.
    uptime = status()['uptime_s']
    for size in (32769, 40000, 60000, 100000):
        check(f'oversize upload ({size} bytes) gets a clear refusal', api('PUT', f'/api/file?path={path}', b'\x55' * size)[0] == 413)
    largest = good + b'\x00' * (32768 - len(good))
    check('picture at the 32 KB limit is accepted', api('PUT', f'/api/file?path={path}', largest)[0] == 200)
    check('picture at the limit reads back', api('GET', f'/api/file?path={path}')[1] == largest)
    check('smaller picture replaces it', api('PUT', f'/api/file?path={path}', good)[0] == 200)

    def snapshot(port):
        c = http.client.HTTPConnection(HOST, port, timeout=5)
        # With a login, the address carries a token only the hub can make, so it comes from the hub, as an NVR's does.
        given = re.search(r'<tt:Uri>http://[^/]+([^<]+)</tt:Uri>', soap('GetSnapshotUri')[1]).group(1) if BOARD_LOGIN else '/onvif/snapshot'
        c.request('GET', given)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, r.getheader('Content-Type'), data

    code, kind, data = snapshot(ONVIF)
    check('ONVIF snapshot is a JPEG', code == 200 and kind == 'image/jpeg' and data.startswith(b'\xff\xd8\xff'), (code, kind))
    check('snapshot is not on the web port', snapshot(WEB)[0] == 404)
    check('ONVIF gives a snapshot address', '/onvif/snapshot' in soap('GetSnapshotUri')[1])
    jpg = path[:-5] + '.jpg'
    tiny = b'\xff\xd8\xff\xe0' + b'\x01' * 60
    check('thumbnail must be a JPEG', api('PUT', f'/api/file?path={jpg}', b'not a jpeg at all')[0] == 422)
    check('thumbnail is saved', api('PUT', f'/api/file?path={jpg}', tiny)[0] == 200)
    check('thumbnail reads back', api('GET', f'/api/file?path={jpg}')[1] == tiny)
    check('bad thumbnail name is refused', api('PUT', f'/api/file?path={path[:-5]}x.jpg', tiny)[0] == 400)
    listed = [f for s in json.loads(api('GET', '/api/tree')[1])['nodes'] for n in s['sensors'] for f in n['files'] if f['name'] == 'approaching.h264']
    check('tree says a picture has a thumbnail', any(f['preview'] for f in listed), listed)
    check('tree does not list thumbnails as pictures', not [f for s in json.loads(api('GET', '/api/tree')[1])['nodes'] for n in s['sensors'] for f in n['files'] if f['name'].endswith('.jpg')])
    get(f'/sensor?{sid(4)}&direction=1')
    check('snapshot shows the picture being sent', wait_until(lambda: snapshot(ONVIF)[2] == tiny))
    # Video is sent straight from the picture, with no buffer per viewer, so all of these work at once. The hub takes
    # two viewers, and on a board the NVR is already one of them.
    seen = {}
    viewers = [threading.Thread(target=lambda k=k: seen.update({k: grab_colour()})) for k in (('a',) if DEVICE else ('a', 'b'))]
    for v in viewers:
        v.start()
    time.sleep(1)
    download, snap = api('GET', f'/api/file?path={path}'), snapshot(ONVIF)
    for v in viewers:
        v.join()
    check('viewers, a download and a snapshot at once all work', download == (200, good) and snap[0] == 200 and all(not c.startswith('no frame') for c in seen.values()),
          (download[0], snap[0], seen))
    clear(4)
    # The picture changes under a running stream, which keeps going.
    login = f'admin:{urllib.parse.quote(BOARD_LOGIN, safe="")}@' if BOARD_LOGIN else ''
    viewer = subprocess.Popen(['ffmpeg', '-v', 'error', '-rtsp_transport', 'tcp', '-i', f'rtsp://{login}{HOST}:{RTSP}/stream', '-t', '8', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    counted = {}
    reader = threading.Thread(target=lambda: counted.update(bytes=len(viewer.stdout.read())))
    reader.start()
    for _ in range(4):
        get(f'/sensor?{sid(4)}&direction=1')
        time.sleep(1)
        clear(4)
        time.sleep(1)
    reader.join(30)
    viewer.wait(30)
    check('a stream keeps going while the picture changes', counted.get('bytes', 0) // (640 * 360 * 3) >= 10, counted)
    check('replacing a picture removes its old thumbnail', api('PUT', f'/api/file?path={path}', good)[0] == 200 and api('GET', f'/api/file?path={jpg}')[0] == 404)
    api('PUT', f'/api/file?path={jpg}', tiny)
    check('deleting a picture removes its thumbnail', api('DELETE', f'/api/file?path={path}')[0] == 200 and api('GET', f'/api/file?path={jpg}')[0] == 404)
    check('picture can be restored', api('PUT', f'/api/file?path={path}', good)[0] == 200)
    check('hub did not restart after very large uploads', status()['uptime_s'] >= uptime, status())
    check('refused uploads leave the file intact', api('GET', f'/api/file?path={path}')[1] == good)

    for bad in ('../evil.h264', '..', f'{A}/../../evil.h264', f'{name(1)}/evil.h264', f'{A}/999', 'a/b/c/d'):
        check(f'bad path refused: {bad}', api('PUT', f'/api/file?path={bad}', good)[0] == 400)
    check('delete with bad path refused', api('DELETE', '/api/file?path=../x')[0] == 400)
    check('delete of nothing reports it', api('DELETE', f'/api/file?path={name(1)}/rtl.h264')[0] == 404)

    tree = json.loads(api('GET', '/api/tree')[1])
    by_mac = {s['mac']: s for s in tree['nodes']}
    sensors_b = {s['input']: s for s in by_mac[B]['sensors']}
    check('tree shows nodes and sensors', set(by_mac) >= {A, B} and sorted(s['input'] for s in by_mac[A]['sensors']) == [1, 2, 3], tree)
    check('tree shows the file and its size', sensors_b[1]['files'] == [{'name': 'approaching.h264', 'size': len(good), 'preview': False}], sensors_b[1])

    words = json.dumps({'label': 'FRONT DOOR', 'active': 'SOMEONE THERE'}).encode()
    text = f'{name(4)}/text.json'
    check('sensor text can be saved', api('PUT', f'/api/file?path={text}', words)[0] == 200)
    check('sensor text reads back', api('GET', f'/api/file?path={text}') == (200, words))
    check('oversize text is refused', api('PUT', f'/api/file?path={text}', b'x' * 1025)[0] == 413)
    check('text file is not listed as a picture', sensors_b[1]['files'] == [{'name': 'approaching.h264', 'size': len(good), 'preview': False}])
    if hub_files_free:
        check('hub text can be saved', api('PUT', '/api/file?path=text.json', b'{"clear":"QUIET"}')[0] == 200)
        check('hub text can be deleted', api('DELETE', '/api/file?path=text.json')[0] == 200)
    check('text path cannot be a folder', api('POST', f'/api/mkdir?path={text}')[0] == 400)
    full = api('GET', '/')
    check('whole page arrives', full[1].rstrip().endswith(b'</html>') and b'Make and save 10 pictures' in full[1] and b'Use an image' in full[1], len(full[1]))

    check('picture can be deleted', api('DELETE', f'/api/file?path={path}')[0] == 200)
    check('deleted picture is gone', api('GET', f'/api/file?path={path}')[0] == 404)
    # Only these two exist for sensor 4, so a report with a direction has to fall back to them.
    for f in ('active.h264', 'active-delayed.h264'):
        api('PUT', f'/api/file?path={name(4)}/{f}', files[f])


def main():
    print('making test pictures', flush=True)
    files = make_frames.sensor_files('TEST')
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage:
        proc = None if DEVICE else start_hub(storage, PASSWORD)
        try:
            if proc:
                check('server starts', HUB.stdout.readline().strip() == 'ready')
            # An NVR is listening before any alert, as in use. Without one, the hub would keep every alert for a replay.
            puller = PullingNvr()
            codes = [api('GET', f'/api/file?path={p}')[0] for p in ('clear.h264', 'text.json')]
            if any(c not in (200, 404) for c in codes):
                sys.exit(f'stopping: cannot tell whether the board has its own files (HTTP {codes})')
            hub_files_free = codes == [404, 404]
            if not hub_files_free:
                print('SKIP the board has its own clear.h264 or text.json, so checks that replace them are skipped', flush=True)
            file_manager_tests(files, hub_files_free)

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
                check('clear.h264 can be uploaded', api('PUT', '/api/file?path=clear.h264', files['active.h264'])[0] == 200)
                colour = grab_colour()
                check('uploaded clear picture is streamed', colour == 'red', colour)
                if PASSWORD:
                    half = socket.create_connection((HOST, WEB), timeout=5)
                    half.sendall(b'PUT /api/file?path=clear.h264 HTTP/1.1\r\nHost: x\r\nContent-Length: 30000\r\n\r\nabc')
                    time.sleep(1.2)  # time for the hub to wrongly accept it
                    check('an upload with no login leaves the picture alone', json.loads(api('GET', '/api/live')[1])['file'] == 'clear.h264')
                    half.close()
                check('clear.h264 can be deleted', api('DELETE', '/api/file?path=clear.h264')[0] == 200)
                colour = grab_colour()
                check('built-in clear picture returns', colour == 'green', colour)

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
            # Waited for rather than slept: over a slow network the last activation lands late, and its hold ends later.
            wait_until(lambda: 'false' in nvr.states()[before:], timeout=HOLD_MS / 1000 * 0.4 + 6)
            check('the alert ends when the hold after the last activation runs out', nvr.states()[before:][-1:] == ['false'] and nvr.states()[before:].count('false') == 1, nvr.states()[before:])

            # The Live page shows the picture being sent. The picture changes on the next frame after the alert ends.
            time.sleep(1.5)
            idle = json.loads(api('GET', '/api/live')[1])
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

            # The ESP-NOW page lists every sensor node.
            node_list = json.loads(api('GET', '/api/espnow')[1])
            entry = [n for n in node_list['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
            check('ESP-NOW page lists a sensor that has reported', len(entry) == 1 and entry[0]['state'] == 'idle' and entry[0]['ago'] is not None, node_list)
            quiet = [n for n in node_list['nodes'] if n['node'] == SENSORS[6][0] and n['input'] == SENSORS[6][1]]
            check('ESP-NOW page shows the state of each sensor', len(quiet) == 1 and quiet[0]['state'] in ('never', 'idle'), node_list)
            get(f'/sensor?{sid(2)}')
            wait_until(lambda: name(2) in status()['active'])
            firing = [n for n in json.loads(api('GET', '/api/espnow')[1])['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
            check('ESP-NOW page shows an active sensor', firing and firing[0]['state'] == 'active', firing)
            clear(2)
            check('ESP-NOW page needs the web password', not PASSWORD or api('GET', '/api/espnow', auth=False)[0] == 401)

            if not DEVICE:  # the PC build stands in for the radio
                def radio():
                    return json.loads(api('GET', '/api/espnow')[1])

                def node_entry(d):
                    return [n for n in d['nodes'] if n['node'] == SENSORS[1][0] and n['input'] == SENSORS[1][1]][0]

                control('radio-known')
                seen = node_entry(radio())
                check('radio report shows signal, battery and report number', (seen['rssi'], seen['battery'], seen['counter']) == (-61, 55, 77), seen)
                to, _, sent = control('last-sent').partition(' ')
                expect = bytes([3, 1, 0]) + (77).to_bytes(4, 'little')
                signature = hmac.new(b'test-espnow-key-0123456789', bytes.fromhex(to) + expect, hashlib.sha1).digest()[:8]
                check('the hub confirms a report to its sensor, signed for that sensor', to == '020000000001' and sent == (expect + signature).hex(), (to, sent))
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
                info = radio()
                check('the ESP-NOW page shows the key, and that unsigned messages are refused', info['key'] == 'test-espnow-key-0123456789' and info['unsigned'] is False, info)
                before = node_entry(radio())
                control('radio-unsigned')
                after = node_entry(radio())
                check('an unsigned message is refused', after['refused'] == before['refused'] + 1 and after['refusal'] == 'not signed' and after['counter'] == before['counter'], after)
                check('unsigned messages can be allowed', api('POST', '/api/espnow', 'unsigned=1')[0] == 200 and radio()['unsigned'] is True)
                control('radio-unsigned')
                check('an unsigned message is accepted when allowed', node_entry(radio())['counter'] != after['counter'] and node_entry(radio())['refused'] == after['refused'], node_entry(radio()))
                api('POST', '/api/espnow', 'unsigned=0')
                control('last-sent')
                control('radio-forged')
                check('a message signed with another key is refused', node_entry(radio())['refusal'] == 'wrong signature', node_entry(radio()))
                check('a refused message gets no confirmation', control('last-sent') == '')
                control('radio-known')
                before = node_entry(radio())
                control('radio-old')
                after = node_entry(radio())
                check('a signed message with a lower number is still accepted, so no alert is lost', after['refused'] == before['refused'] and after['counter'] == before['counter'] - 9, (before, after))
                events = len(json.loads(api('GET', '/api/events')[1]))
                control('radio-restart-alert')
                after = node_entry(radio())
                history = json.loads(api('GET', '/api/events')[1])
                check('a sensor that restarts its count at 1 is heard', after['counter'] == 1 and after['refused'] == before['refused'], after)
                check("a restarted sensor's alert reaches the NVR", name(1) in status()['active'] and history[0]['active'] is True and len(history) > events, history[:1])
                control('radio-restart-alert')
                check('the same report again a moment later is a resend and ignored', len(json.loads(api('GET', '/api/events')[1])) == len(history))
                clear(1)

                # Weak signal.
                control('radio-weak')
                check('a weak signal is flagged', node_entry(radio())['weak'] is True, node_entry(radio()))
                check('a weak signal is in the event history', json.loads(api('GET', '/api/events')[1])[0]['kind'] == 4)
                for _ in range(6):
                    control('radio-known')
                check('a good signal clears the flag', node_entry(radio())['weak'] is False, node_entry(radio()))
                clear(1)

                # Sensor names.
                label = 'Front "gate"'.encode()
                check('a sensor can be named', api('PUT', f'/api/file?path={name(1)}/name.txt', label)[0] == 200)
                check('the name is shown with the node', node_entry(radio())['name'] == label.decode(), node_entry(radio()))
                check('the name is on the Sensors page data', any(n.get('name') == label.decode() for s in json.loads(api('GET', '/api/tree')[1])['nodes'] for n in s['sensors']))

                # Battery and silence alerts.
                control('radio-low')
                check('low battery is flagged', node_entry(radio())['low'] is True, node_entry(radio()))
                history = json.loads(api('GET', '/api/events')[1])
                check('low battery is in the event history', history and history[0]['kind'] == 2 and history[0]['name'] == label.decode(), history[:1])
                before_again = len(json.loads(api('GET', '/api/events')[1]))
                control('radio-low')
                check('low battery alerts only once', len(json.loads(api('GET', '/api/events')[1])) == before_again)
                control('radio-known')
                check('a good battery clears the flag', node_entry(radio())['low'] is False, node_entry(radio()))
                check('the alarm setting is saved', api('POST', '/api/alerts', 'alarm=1')[0] == 200 and json.loads(api('GET', '/api/alerts')[1])['alarm'] is True)
                control('radio-low')
                wait_until(lambda: node_entry(radio())['state'] == 'active')
                check('a low battery can raise the alarm', node_entry(radio())['state'] == 'active', node_entry(radio()))
                clear(1)
                control('radio-known')
                api('POST', '/api/alerts', 'alarm=0')

                # Per-sensor settings.
                path = name(1)

                def tree_entry():
                    return [n for s in json.loads(api('GET', '/api/tree')[1])['nodes'] if s['mac'] == SENSORS[1][0] for n in s['sensors'] if n['input'] == SENSORS[1][1]][0]

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
                minute = int(time.time() // 60 % 1440)
                api('POST', f'/api/sensor-settings?path={path}', f'from={(minute - 2) % 1440}&to={(minute + 3) % 1440}')
                check('the Sensors page data shows the quiet hours', tree_entry()['quiet'] == [(minute - 2) % 1440, (minute + 3) % 1440], tree_entry())
                get(f'/sensor?{sid(1)}')
                wait_until(lambda: event_list()[0]['kind'] == 3)  # logged, so the hub has decided
                check('an alert in quiet hours does not reach the NVR', path not in status()['active'])
                check('an alert in quiet hours is still logged', json.loads(api('GET', '/api/events')[1])[0]['kind'] == 3)
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

                # The full event history.
                code, csv = api('GET', '/api/events.csv')
                lines = csv.decode().splitlines()
                check('the full history downloads as CSV', code == 200 and lines[0].startswith('received_utc,') and any(f',{path},' in l and ',alert in quiet hours,' in l for l in lines), lines[:3])
                check('the history is sent in chunks, so a download cut short shows as failed', last_transfer == 'chunked', last_transfer)
                check('a name with quotes stays one CSV field', any('"Front ""gate"""' in l for l in lines), [l for l in lines if path in l][:2])
                check('the full history needs the web password', api('GET', '/api/events.csv', auth=False)[0] == 401)

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
                hook_state = poll(lambda: json.loads(api('GET', '/api/webhook')[1]), lambda h: h['waiting'] == 0 and h['last'])
                check('the page shows how the last message went', hook_state['last'] == 'HTTP 200' and hook_state['waiting'] == 0, hook_state)
                api('POST', '/api/webhook', f'url={hook_url}&text=1&alerts=1&problems=0')
                get(f'/sensor?{sid(1)}')
                check('plain text is sent when chosen', hooks(3) and received[2][0].startswith('text/plain') and received[2][1].decode().endswith(': alert'), received[2:])
                clear(1)
                control('radio-low')
                time.sleep(0.5)  # time for a message to be wrongly sent
                check('node problems are not sent when turned off', len(received) == 3, received[3:])
                control('radio-known')
                check('the webhook settings need the web password', api('GET', '/api/webhook', auth=False)[0] == 401)
                api('POST', '/api/webhook', 'url=&alerts=1&problems=1')
                catcher.shutdown()
                check('the event history needs the web password', api('GET', '/api/events', auth=False)[0] == 401)
                check('/status needs the web password', http_status('/status') == 401)
                control('memory-short')
                code = api('GET', '/api/events')[0]
                control('memory-normal')
                check('a reply larger than the memory left fails alone, and the hub keeps running', code == 507 and api('GET', '/api/events')[0] == 200, code)

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
                control('nvr-gone-normal')
                api('POST', '/api/nvr', 'ip=')

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

                # A live alert never shares its NVR record with a past event: the past event stops and plays again later.
                get(f'/sensor?{sid(2)}&age=500')
                playing = wait_until(lambda: name(2) in status()['active'], timeout=20)
                get(f'/sensor?{sid(1)}')
                now_on = status()
                check('a live alert stops a past event that was playing', playing and now_on['active'] == [name(1)] and now_on['delayed_waiting'] >= 1, now_on)
                control('end-alerts')
                check('and the past event plays again once the live one is over', wait_until(lambda: name(2) in status()['active'], timeout=20), status())
                control('end-alerts')

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

            # Other clients may poll instead.
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

            # WS-Discovery, sent directly.
            udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            udp.settimeout(3)
            probe = ('<e:Envelope xmlns:e="http://www.w3.org/2003/05/soap-envelope" xmlns:w="a" xmlns:d="b" xmlns:dn="c"><e:Header><w:MessageID>uuid:test-1</w:MessageID>'
                     '</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body></e:Envelope>')
            udp.sendto(probe.encode(), (HOST, DISCOVERY))
            reply = udp.recv(4096).decode()
            check('discovery answers a Probe', 'ProbeMatch' in reply and 'uuid:test-1' in reply and f'http://{HOST}:{ONVIF}/onvif/device_service' in reply, reply[:300])

            # A slightly old report still counts as live.
            get(f'/sensor?{sid(6)}&age=5')
            now = status()
            check('recent report is treated as live', now['active'] == [name(6)], now)
            clear(6)
            check('live report is not marked delayed', json.loads(get('/events')[1])[1]['delayed'] is False)

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

            # Each node sets how old an alert must be to count as a past event. Sensor 3 has no pictures.
            node3 = SENSORS[3][0]
            check('a node can have its own past-event time', api('POST', f'/api/node-settings?path={node3}', 'late=300')[0] == 200)
            check('the Sensors page data shows it', [s for s in json.loads(api('GET', '/api/tree')[1])['nodes'] if s['mac'] == node3][0]['late'] == 300)
            check("it is kept in the node's folder", api('GET', f'/api/file?path={node3}/settings.txt') == (200, b'late=300\n'))
            get(f'/sensor?{sid(3)}&age=100')
            wait_until(lambda: status()['active'] == [name(3)])
            check('an alert younger than the node\'s time is live', status()['active'] == [name(3)] and json.loads(get('/events')[1])[0]['delayed'] is False, json.loads(get('/events')[1])[0])
            colour = grab_colour()
            check('a live alert with no pictures shows the built-in alert', colour == 'red', colour)
            clear(3)
            get(f'/sensor?{sid(3)}&age=400')
            wait_until(lambda: status()['active'] == [name(3)])
            check('an alert older than the node\'s time is a past event', json.loads(get('/events')[1])[0]['delayed'] is True and status()['active'] == [name(3)], json.loads(get('/events')[1])[0])
            colour = grab_colour()
            check('a past event with no pictures shows the built-in past-event picture', colour == 'amber', colour)
            clear(3)
            check('a bad past-event time is refused', api('POST', f'/api/node-settings?path={node3}', 'late=0')[0] == 400)
            check('a node can have its own silent time', api('POST', f'/api/node-settings?path={node3}', 'late=300&silent=600')[0] == 200
                  and api('GET', f'/api/file?path={node3}/settings.txt') == (200, b'late=300\nsilent=600\n'))
            check('the Sensors page data shows it', [s for s in json.loads(api('GET', '/api/tree')[1])['nodes'] if s['mac'] == node3][0]['silent'] == 600)
            check('a silent time under a minute is refused', api('POST', f'/api/node-settings?path={node3}', 'silent=30')[0] == 400)
            check('settings for a node that does not exist are refused', api('POST', '/api/node-settings?path=02-00-00-00-00-77', 'late=60')[0] == 404)
            check('clearing the node settings removes the file', api('POST', f'/api/node-settings?path={node3}', '')[0] == 200 and api('GET', f'/api/file?path={node3}/settings.txt')[0] == 404)
            get(f'/sensor?{sid(3)}&age=100')
            wait_until(lambda: event_list()[0]['active'])
            check('without the setting, 100 seconds is a past event again', json.loads(get('/events')[1])[0]['delayed'] is True)
            clear(3)

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

            # A delayed alert belongs to its device. A new sensor that takes a deleted one's place must not play it.
            gone, newcomer = '02-00-00-00-44-01', '02-00-00-00-44-02'
            api('POST', f'/api/mkdir?path={gone}/1')
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

            # Until the queue is empty, which may first wait out an earlier past event's gap, then through the time it
            # would have played for, since it must not appear.
            poll(watched, lambda st: st['delayed_waiting'] == 0, timeout=2 * (HOLD_MS / 1000 + 4) + 5, step=0.2)
            end = time.time() + HOLD_MS / 1000 + 1
            while time.time() < end:
                watched()
                time.sleep(0.2)
            check('a deleted sensor\'s delayed alert does not play as another sensor', f'{newcomer}/1' not in shown and status()['delayed_waiting'] == 0, shown)
            api('DELETE', f'/api/file?path={newcomer}')

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

            # Deleting folders removes the sensors, without a reboot.
            count = status()['sensors']
            check('sensor folder can be deleted', api('DELETE', f'/api/file?path={name(6)}')[0] == 200)
            check('deleted sensor is unknown at once', get(f'/sensor?{sid(6)}')[0] == 404 and status()['sensors'] == count - 1)
            check('node folder can be deleted with its contents', api('DELETE', f'/api/file?path={A}')[0] == 200)
            check('all its sensors are gone', get(f'/sensor?{sid(1)}')[0] == 404 and status()['sensors'] == count - 4)

            if proc:
                # A past event still waiting when the power goes must play after the restart: its sensor was told it was received.
                get(f'/sensor?{sid(4)}')  # keeps it waiting
                get(f'/sensor?{sid(5)}&age=500')
                check('a past event waits while another alert is on', status()['delayed_waiting'] >= 1, status())
                proc.kill()
                proc.wait()
                proc = start_hub(storage, PASSWORD, '1500')
                HUB.stdout.readline()
                puller.stop()  # its subscription was in the hub that stopped
                puller = PullingNvr()
                check('folders are still sensors after a restart', status()['sensors'] == count - 4, status())
                check('stored pictures survive a restart', api('GET', f'/api/file?path={name(4)}/active.h264')[1] == files['active.h264'])
                check('a past event waiting at the restart still plays', wait_until(lambda: name(5) in status()['active']), status())
                control('end-alerts')
                quiet_nodes = poll(lambda: json.loads(api('GET', '/api/espnow')[1])['nodes'], lambda n: n and all(x['state'] == 'silent' for x in n))
                check('a node quiet since the restart is reported silent', quiet_nodes and all(n['state'] == 'silent' for n in quiet_nodes), quiet_nodes)
                check('going silent after a restart is logged', any(e['kind'] == 1 for e in json.loads(api('GET', '/api/events')[1])))
                starts = [e for e in event_list() if e['kind'] == 8]
                check('the start is logged with its reason and how long the hub was off', starts and starts[0]['sensor'] == 'hub' and starts[0]['detail'] == 'PC build started'
                      and starts[0]['event'] == 'hub started' and starts[0]['off_s'] is not None and starts[0]['off_s'] < 3600, starts[:2])
            api('DELETE', f'/api/file?path={B}')
        finally:
            if proc:
                proc.kill()
                proc.wait()
    print(f'\n{len(failures)} failure(s)' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
