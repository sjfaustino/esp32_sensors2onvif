"""What every part of the end-to-end test shares: where the hub is, how to ask it things, and the pretend NVR and
MQTT broker. See e2e_test.py for how to run it."""
import base64, datetime, gzip, hashlib, hmac, http.client, http.server, json, os, re, socket, subprocess, sys, tempfile, threading, time, urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_frames

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get('E2E_EXE') or os.path.join(ROOT, '.pio', 'build', 'host', 'program.exe')  # tools/coverage.py names its own


def signed(image, release=4000, key_file='test/firmware-test-key.pem'):
    """The image signed for the PC build, which takes updates signed with the test key from release 0.4.0 (4000) on
    (tools/sign_firmware.py). release=None signs it as before release numbers."""
    import sign_firmware
    from cryptography.hazmat.primitives import serialization
    with open(os.path.join(ROOT, key_file), 'rb') as f:
        key = serialization.load_pem_private_key(f.read(), password=None)
    if release is None:  # the first signed format, without a release number
        import hashlib
        return image + key.sign(sign_firmware.CONTEXT + hashlib.sha512(image).digest()) + sign_firmware.MAGIC
    return sign_firmware.sign(image, key, release)


def signed_node(image, release=0, key_file='test/firmware-test-key.pem'):
    """A node's firmware signed with the test key, as the node project's (sign_firmware.py --node)."""
    import sign_firmware
    from cryptography.hazmat.primitives import serialization
    with open(os.path.join(ROOT, key_file), 'rb') as f:
        return sign_firmware.sign(image, serialization.load_pem_private_key(f.read(), password=None), release, node=True)


def other_key_signed(image):
    """Signed with a key the hub does not know."""
    import sign_firmware
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    return sign_firmware.sign(image, Ed25519PrivateKey.generate(), 4000)
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


def soap(action, path='/onvif/media', inner='', source=None):
    header = wsse_header(BOARD_LOGIN) if BOARD_LOGIN else ''
    body = (f'<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">{header}<s:Body>'
            f'<{action} xmlns="x">{inner}</{action}></s:Body></s:Envelope>')
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5, source_address=(source, 0) if source else None)
    c.request('POST', path, body, {'Content-Type': 'application/soap+xml', 'Host': f'{HOST}:{ONVIF}'})
    r = c.getresponse()
    data = r.read().decode()
    c.close()
    return r.status, data


def body_of(r):
    """The reply's body, unzipped if the hub sent it gzipped, as it does the web pages. Notes how it came."""
    global last_encoding, last_transfer, last_cache
    last_encoding = r.getheader('Content-Encoding')
    last_cache = r.getheader('Cache-Control')
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


def api(method, path, body=None, auth=True, origin=None, headers=None):
    """Returns (status, body bytes)."""
    headers = dict(headers or {})
    if origin:
        headers['Origin'] = origin
    if auth and PASSWORD:
        headers['Authorization'] = 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()
    c = http.client.HTTPConnection(HOST, WEB, timeout=15)
    c.request(method, path, body, headers)
    r = c.getresponse()
    data = body_of(r)
    c.close()
    return r.status, data


def get_json(path):
    """A page that answers JSON, read with the web password."""
    return json.loads(api('GET', path)[1])


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
    return get_json('/api/events')


def live():
    return get_json('/api/live')


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

    def send(self, topic, payload, retained=False):
        """Publishes to the hub at QoS 0, as Home Assistant's switch does; retained, as a broker repeats a kept message."""
        t = topic.encode()
        body = bytes([len(t) >> 8, len(t) & 0xFF]) + t + payload
        self.conn.sendall(bytes([0x31 if retained else 0x30, len(body)]) + body)

    def latest(self, topic):
        found = [m for m in self.messages if m['topic'] == topic]
        return found[-1] if found else None

    def state(self, topic):
        m = self.latest(topic)
        return json.loads(m['payload']) if m and m['payload'] else None

    def events(self, topic):
        return [m for m in self.messages if m['topic'] == topic and m['qos'] == 1]


# ---- the tests, each on its own ----
#
# Every test is a function taking the Session, registered with @case. Each starts from a quiet hub (quiet_hub): no alert
# on, no past event waiting, armed away, no NVR address, and the six test sensors there as Session describes. It sets up
# whatever else it needs, and puts back what it changed. So any test can run alone, in any order, and one that fails or
# stops part way leaves the next to start as if it had not run.

SENSOR_1_NAME = 'Front "gate"'  # a name with quotes, so every place a name goes is tested with them


class Case:
    def __init__(self, func, pc, free, last):
        self.func, self.pc, self.free, self.last = func, pc, free, last
        self.name = func.__name__


CASES = []


def case(pc=False, free=False, last=False):
    """Registers a test. pc: it needs the PC build's test commands or its radio, so a board skips it. free: it replaces
    the hub's own clear.h264 or text.json, so a board that has its own skips it. last: it restarts the hub or deletes
    the test sensors, so it runs after the others."""
    def register(func):
        CASES.append(Case(func, pc, free, last))
        return func
    return register


class Session:
    """The hub under test and what every test shares: a pretend NVR that pulls events, as one must for the hub to count
    it listening, and one that is pushed them (nvr), and the test pictures. Sensors 1 to 3 are on node A, 4 to 6 on node
    B; sensor 1 is named SENSOR_1_NAME, and sensor 4 has only an alert picture and its past-event version."""

    def __init__(self, storage, files):
        self.storage, self.files = storage, files
        self.proc = None
        if not DEVICE:
            self.proc = start_hub(storage, PASSWORD)
            if HUB.stdout.readline().strip() != 'ready':
                sys.exit('the PC build did not start')
        codes = [api('GET', f'/api/file?path={p}')[0] for p in ('clear.h264', 'text.json')]
        if any(c not in (200, 404) for c in codes):
            sys.exit(f'stopping: cannot tell whether the board has its own files (HTTP {codes})')
        self.hub_files_free = codes == [404, 404]
        if not self.hub_files_free:
            print('SKIP the board has its own clear.h264 or text.json, so the tests that replace them are skipped', flush=True)
        self.puller = PullingNvr()
        self.nvr = FakeNvrListener()
        self.subscribe()

    def subscribe(self):
        inner = f'<ConsumerReference><Address>http://{LOCAL}:{self.nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
        soap('Subscribe', '/onvif/device_service', inner)

    def restart(self, *args):
        """Stops the PC build as a power cut would, and starts it again on the same storage."""
        self.proc.kill()
        self.proc.wait()
        self.proc = start_hub(self.storage, PASSWORD, *args)
        HUB.stdout.readline()
        self.puller.stop()  # its subscription was in the hub that stopped
        self.puller = PullingNvr()
        self.subscribe()

    def stop(self):
        if self.proc:
            self.proc.kill()
            self.proc.wait()


# What a test may have left set in the PC build when it stopped part way, put back before the next.
PC_DEFAULTS = ('auth-off', 'memory-normal', 'disk-normal', 'lists-work', 'stats-work', 'drain-normal', 'firmware-allowed',
               'nvr-gone-normal', 'overdue-ms 0', 'busy-alerts 100000')


def quiet_hub(s):
    """The state every test starts from."""
    if DEVICE:
        wait_until(lambda: (lambda st: not st['active'] and not st['delayed_waiting'])(status()), timeout=120, step=0.5)
    else:
        for command in PC_DEFAULTS:
            control(command)
        control('forget-waiting')
        control('end-alerts')
    if get_json('/api/arm')['mode'] != 'away':
        api('POST', '/api/arm', 'mode=away')
    if get_json('/api/nvr')['ip']:
        api('POST', '/api/nvr', 'ip=')
    api('POST', '/api/pairing/close', 'x=1')
    nodes = {n['mac']: n for n in get_json('/api/tree')['nodes']}
    for n, (mac, number) in SENSORS.items():
        if not any(x['input'] == number for x in nodes.get(mac, {}).get('sensors', [])):
            api('POST', f'/api/mkdir?path={name(n)}')
    sensors = {f"{mac}/{x['input']}": x for mac, node in nodes.items() for x in node['sensors']}
    if sensors.get(name(1), {}).get('name') != SENSOR_1_NAME:
        api('POST', f'/api/sensor-settings?path={name(1)}', 'name=' + urllib.parse.quote(SENSOR_1_NAME))
    pictures = {f['name'] for f in sensors.get(name(4), {}).get('files', [])}
    for f in ('active.h264', 'active-delayed.h264'):
        if f not in pictures:
            api('PUT', f'/api/file?path={name(4)}/{f}', s.files[f])


def run_cases(s, words):
    """Runs the tests named, or with any of words in their names, or all of them; each to its end, whatever the others
    did. A word that is a test's whole name picks that test alone."""
    names = {c.name for c in CASES}
    chosen = [c for c in CASES if not words or any(w == c.name or (w not in names and w in c.name) for w in words)]
    chosen.sort(key=lambda c: c.last)
    if not chosen:
        sys.exit('no test has any of these in its name: ' + ' '.join(words))
    failed = []
    for c in chosen:
        if c.pc and DEVICE:
            print(f'SKIP {c.name}: it needs the PC build', flush=True)
            continue
        if c.free and not s.hub_files_free:
            print(f'SKIP {c.name}: it would replace the board\'s own files', flush=True)
            continue
        print(f'\n-- {c.name}', flush=True)
        before = len(failures)
        try:
            quiet_hub(s)
            c.func(s)
        except (OSError, http.client.HTTPException, ValueError, KeyError, IndexError, TypeError, AttributeError) as e:
            import traceback
            traceback.print_exc()
            check(f'{c.name} runs to its end', False, repr(e))
        if len(failures) > before:
            failed.append(c.name)
    return failed


def main_of(title):
    """The command line of a test file: its test names to run, or none for all."""
    import argparse
    parser = argparse.ArgumentParser(description=title)
    parser.add_argument('words', nargs='*', help='run only the tests with any of these in their names')
    parser.add_argument('--list', action='store_true', help='list the tests and stop')
    args = parser.parse_args()
    if args.list:
        for c in sorted(CASES, key=lambda c: c.last):
            print(c.name + (' (PC build only)' if c.pc else ''))
        sys.exit(0)
    return args.words


def finish(failed):
    if failed:
        print(f'\n{len(failures)} failure(s), in: ' + ' '.join(failed))
        print('to run just those again: py tools/e2e_test.py ' + ' '.join(failed))
    else:
        print('\nall passed')
    sys.exit(1 if failures else 0)


# ---- sealed backups: the page works out the key from the passphrase, as these do with Python's own PBKDF2 ----

PASSPHRASE = 'correct horse battery'


def sealed_backup(passphrase=PASSPHRASE, rounds=100000, auth=True):
    """A sealed settings backup, as the page asks for one. Returns (status, body). Fewer rounds than the page's, to be quick."""
    salt = os.urandom(16)
    key = hashlib.pbkdf2_hmac('sha256', passphrase.encode(), salt, rounds)
    return api('POST', '/api/backup', f'key={key.hex()}&salt={salt.hex()}&rounds={rounds}', auth=auth)


def backup_key(sealed, passphrase=PASSPHRASE):
    """The header that opens a sealed backup, with the key worked out from the rounds and salt on its first line."""
    head = re.match(rb'sensorcam-backup sealed 2 pbkdf2-sha256 (\d+) ([0-9a-f]{32})\n', sealed)
    if not head:
        return {}
    key = hashlib.pbkdf2_hmac('sha256', passphrase.encode(), bytes.fromhex(head.group(2).decode()), int(head.group(1)))
    return {'X-Backup-Key': key.hex()}
