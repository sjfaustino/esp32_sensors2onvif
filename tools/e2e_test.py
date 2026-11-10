"""End-to-end test of the firmware library, built for the PC (pio run -e host).

Plays the NVR's part using the call order recorded from the real NVR. By default every socket is
loopback. Needs ffmpeg.

To test a flashed board instead, restart it first, then set E2E_DEVICE to its address and
E2E_LOCAL_IP to this PC's address on the same network. If the board has a password, set
E2E_WEB_PASSWORD to it and E2E_SENSOR_TOKEN to the token on its HTTP page. It creates and deletes only the 02-00-00-00-00-0x test folders. If the board
has its own clear.h264 or text.json it skips the checks that would replace them, and it stops
if it cannot tell. Never point E2E_DEVICE at an NVR.
"""
import base64, datetime, hashlib, http.client, json, os, re, socket, subprocess, sys, tempfile, threading, time, urllib.parse

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


def get(path):
    headers = {}
    if PASSWORD:
        headers['Authorization'] = 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()
    if TOKEN and path.startswith('/sensor'):
        path += f'&token={TOKEN}'
    c = http.client.HTTPConnection(HOST, WEB, timeout=5)
    c.request('GET', path, headers=headers)
    r = c.getresponse()
    data = r.read().decode()
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
    data = r.read()
    c.close()
    return r.status, data


def status():
    return json.loads(get('/status')[1])


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
        return 'no frame: ' + out.stderr.decode(errors='replace')[:300]
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


def file_manager_tests(files, hub_files_free):
    if PASSWORD:
        check('file manager needs the password', api('GET', '/', auth=False)[0] == 401)
        check('file operations need the password', api('DELETE', f'/api/file?path={A}', auth=False)[0] == 401)
    for slave in (A, B):
        api('DELETE', f'/api/file?path={slave}')
    baseline = status()['sensors']

    check('endless web headers are dropped without a login', dropped_after(WEB, b'GET / HTTP/1.1\r\nX: ' + b'a' * 8000))
    check('endless ONVIF headers are dropped without a login', dropped_after(ONVIF, b'POST / HTTP/1.1\r\nX: ' + b'a' * 8000))
    check('endless RTSP headers are dropped without a login', dropped_after(RTSP, b'OPTIONS * RTSP/1.0\r\nX: ' + b'a' * 8000))
    own_site = f'http://{HOST}' + ('' if WEB == 80 else f':{WEB}')
    check('another site cannot use a logged-in browser', api('POST', '/api/restore', b'hello', origin='http://evil.example')[0] == 403)
    check('the hub\'s own page is not refused as another site', api('POST', '/api/restore', b'hello', origin=own_site)[0] == 400)

    code, page = api('GET', '/')
    check('page is served', code == 200 and b'SensorCam files' in page, code)
    check('page offers a picture preview', b'title:"Preview"' in page)
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

    code, device = api('GET', '/api/device')
    rows = dict(json.loads(device)['rows']) if code == 200 else {}
    check('device info is served', 'Firmware version' in rows and 'Picture storage' in rows and 'Sensors' in rows, rows)
    check('time settings are served', api('GET', '/api/time')[0] == 200 and 'local' in json.loads(api('GET', '/api/time')[1]))
    code, sched = api('GET', '/api/schedule')
    check('restart schedule is served', code == 200 and 'minute' in json.loads(sched), code)
    check('device info needs the web password', not PASSWORD or api('GET', '/api/device', auth=False)[0] == 401)

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
        check('setup page is served', code == 200 and b'Hub Wi-Fi setup' in page, code)
        check('network list is served', b'test-net' in api('GET', '/api/wifi/scan')[1])
        check('short Wi-Fi password is refused', api('POST', '/api/wifi', 'ssid=x&password=short')[0] == 400)
        check('empty Wi-Fi name is refused', api('POST', '/api/wifi', 'ssid=&password=longenough')[0] == 400)
        secret = 'password=p%25ss%20w%2Brd%26ok'
        check('backup network is saved as the backup', api('POST', '/api/wifi', f'ssid=want-backup&role=backup&{secret}')[0] == 200)
        check('main network is not saved as the backup', api('POST', '/api/wifi', f'ssid=want-backup&role=main&{secret}')[0] == 507)
        check('Wi-Fi password is decoded exactly', api('POST', '/api/wifi', f'ssid=setup-on&{secret}')[0] == 200)
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
        check('leaving setup mode works', api('POST', '/api/wifi', f'ssid=setup-off&{secret}')[0] == 200)
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

    check('slave folder can be added', api('POST', f'/api/mkdir?path={A}')[0] == 200)
    check('slave alone is not a sensor', status()['sensors'] == baseline)
    for n in SENSORS:
        check(f'sensor folder {name(n)} can be added', api('POST', f'/api/mkdir?path={name(n)}')[0] == 200)
    check('new folders become sensors without a reboot', status()['sensors'] == baseline + 6, status())

    good = files['approaching.h264']
    path = f'{name(4)}/approaching.h264'
    check('valid picture is accepted', api('PUT', f'/api/file?path={path}', good)[0] == 200)
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
        c.request('GET', '/onvif/snapshot' + (f"?token={hashlib.sha1(f'snapshot:{BOARD_LOGIN}'.encode()).digest()[:8].hex()}" if BOARD_LOGIN else ''))
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
    listed = [f for s in json.loads(api('GET', '/api/tree')[1])['slaves'] for n in s['sensors'] for f in n['files'] if f['name'] == 'approaching.h264']
    check('tree says a picture has a thumbnail', any(f['preview'] for f in listed), listed)
    check('tree does not list thumbnails as pictures', not [f for s in json.loads(api('GET', '/api/tree')[1])['slaves'] for n in s['sensors'] for f in n['files'] if f['name'].endswith('.jpg')])
    get(f'/sensor?{sid(4)}&direction=1')
    time.sleep(1)
    check('snapshot shows the picture being sent', snapshot(ONVIF)[2] == tiny)
    get(f'/sensor?{sid(4)}&state=clear')
    time.sleep(0.7)
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
    by_mac = {s['mac']: s for s in tree['slaves']}
    sensors_b = {s['input']: s for s in by_mac[B]['sensors']}
    check('tree shows slaves and sensors', set(by_mac) >= {A, B} and sorted(s['input'] for s in by_mac[A]['sensors']) == [1, 2, 3], tree)
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
        proc = None if DEVICE else subprocess.Popen([EXE, str(HOLD_MS), storage, PASSWORD], stdout=subprocess.PIPE, text=True)
        try:
            if proc:
                check('server starts', proc.stdout.readline().strip() == 'ready')
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
            for _ in range(2):
                code, data = soap('Subscribe', device, inner)
                check('Subscribe accepted on device_service', code == 200 and 'SubscribeResponse' in data, data[:200])
            check('two identical Subscribes make one subscriber', status()['subscribers'] == 1)
            time.sleep(0.7)
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
                check('clear.h264 can be deleted', api('DELETE', '/api/file?path=clear.h264')[0] == 200)
                colour = grab_colour()
                check('built-in clear picture returns', colour == 'green', colour)

            # A sensor alert makes a motion event, and the picture shows it. Sensor 3 has no files.
            check('unknown sensor is rejected', get('/sensor?mac=02-00-00-00-00-09')[0] == 404)
            before = len(nvr.events)
            started = time.time()
            code, data = get(f'/sensor?{sid(3)}')
            check('sensor alert accepted', code == 200 and '"ok":true' in data, data)
            time.sleep(0.6)
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

            # Clearing early.
            before = len(nvr.events)
            get(f'/sensor?{sid(2)}')
            time.sleep(0.5)
            check('sensor can be cleared', get(f'/sensor?{sid(2)}&state=clear')[0] == 200)
            time.sleep(0.5)
            check('clear sends false at once', nvr.states()[before:][-1:] == ['false'], nvr.states()[before:])

            # The Live page shows the picture being sent.
            time.sleep(0.7)
            idle = json.loads(api('GET', '/api/live')[1])
            code, idle_frame = api('GET', '/api/live/frame')
            check('Live sends a whole picture', code == 200 and idle_frame.startswith(b'\0\0\0\1') and idle_frame[4] & 0x1F == 7, code)
            get(f'/sensor?{sid(2)}')
            time.sleep(1)
            firing = json.loads(api('GET', '/api/live')[1])
            check('Live reports a new picture when a sensor fires', firing['seq'] != idle['seq'], firing)
            check('Live picture changes when a sensor fires', api('GET', '/api/live/frame')[1] != idle_frame)
            get(f'/sensor?{sid(2)}&state=clear')
            time.sleep(1)
            cleared = json.loads(api('GET', '/api/live')[1])
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
            time.sleep(0.3)
            firing = [n for n in json.loads(api('GET', '/api/espnow')[1])['nodes'] if n['node'] == SENSORS[2][0] and n['input'] == SENSORS[2][1]]
            check('ESP-NOW page shows an active sensor', firing and firing[0]['state'] == 'active', firing)
            get(f'/sensor?{sid(2)}&state=clear')
            check('ESP-NOW page needs the web password', not PASSWORD or api('GET', '/api/espnow', auth=False)[0] == 401)

            if not DEVICE:  # the PC build stands in for the radio
                def radio():
                    return json.loads(api('GET', '/api/espnow')[1])

                def node_entry(d):
                    return [n for n in d['nodes'] if n['node'] == SENSORS[1][0] and n['input'] == SENSORS[1][1]][0]

                api('POST', '/api/wifi', 'ssid=radio-known')
                seen = node_entry(radio())
                check('radio report shows signal, battery and report number', (seen['rssi'], seen['battery'], seen['counter']) == (-61, 55, 77), seen)
                get(f'/sensor?{sid(1)}&state=clear')
                check('a report without a battery keeps the last reading', node_entry(radio())['battery'] == 55, node_entry(radio()))
                check('no unknown senders yet', radio()['unknown'] == [], radio()['unknown'])
                api('POST', '/api/wifi', 'ssid=radio-unknown')
                api('POST', '/api/wifi', 'ssid=radio-unknown')
                stranger = radio()['unknown']
                check('unknown sender is listed', len(stranger) == 1 and stranger[0]['mac'] == '02-00-00-00-00-99' and stranger[0]['input'] == 1 and stranger[0]['rssi'] == -61, stranger)
                check('unknown sender messages are counted', stranger and stranger[0]['messages'] == 2, stranger)
                check('unknown sender is not listed as a node', not [n for n in radio()['nodes'] if n['node'] == '02-00-00-00-00-99'])
                api('POST', '/api/mkdir?path=02-00-00-00-00-99/1')
                check('adding the sender removes it from the unknown list', radio()['unknown'] == [], radio()['unknown'])
                api('DELETE', '/api/file?path=02-00-00-00-00-99')

                # Sensor names.
                label = 'Front "gate"'.encode()
                check('a sensor can be named', api('PUT', f'/api/file?path={name(1)}/name.txt', label)[0] == 200)
                check('the name is shown with the node', node_entry(radio())['name'] == label.decode(), node_entry(radio()))
                check('the name is on the Sensors page data', any(n.get('name') == label.decode() for s in json.loads(api('GET', '/api/tree')[1])['slaves'] for n in s['sensors']))

                # Battery and silence alerts.
                api('POST', '/api/wifi', 'ssid=radio-low')
                check('low battery is flagged', node_entry(radio())['low'] is True, node_entry(radio()))
                history = json.loads(api('GET', '/api/events')[1])
                check('low battery is in the event history', history and history[0]['kind'] == 2 and history[0]['name'] == label.decode(), history[:1])
                before_again = len(json.loads(api('GET', '/api/events')[1]))
                api('POST', '/api/wifi', 'ssid=radio-low')
                check('low battery alerts only once', len(json.loads(api('GET', '/api/events')[1])) == before_again)
                api('POST', '/api/wifi', 'ssid=radio-known')
                check('a good battery clears the flag', node_entry(radio())['low'] is False, node_entry(radio()))
                check('the alarm setting is saved', api('POST', '/api/alerts', 'alarm=1')[0] == 200 and json.loads(api('GET', '/api/alerts')[1])['alarm'] is True)
                api('POST', '/api/wifi', 'ssid=radio-low')
                time.sleep(0.2)
                check('a low battery can raise the alarm', node_entry(radio())['state'] == 'active', node_entry(radio()))
                get(f'/sensor?{sid(1)}&state=clear')
                api('POST', '/api/wifi', 'ssid=radio-known')
                api('POST', '/api/alerts', 'alarm=0')
                check('the event history needs the web password', api('GET', '/api/events', auth=False)[0] == 401)
                check('/status needs the web password', http_status('/status') == 401)

                # Firmware and backup.
                check('firmware update needs the web password', api('PUT', '/api/firmware', b'\xe9' * 100, auth=False)[0] == 401)
                check('valid firmware is accepted', api('PUT', '/api/firmware', b'\xe9' + b'\x01' * 9000)[0] == 200)
                check('other files are refused as firmware', api('PUT', '/api/firmware', b'not firmware' * 500)[0] == 400)
                check('the hub still answers after a firmware upload', status()['uptime_s'] >= 0)
                code, backup = api('GET', '/api/backup')
                check('backup can be downloaded', code == 200 and backup.startswith(b'sensorcam-backup 1'), code)
                check('backup needs the web password', api('GET', '/api/backup', auth=False)[0] == 401)
                check('backup can be restored', api('POST', '/api/restore', backup)[0] == 200)
                check('other files are refused as a backup', api('POST', '/api/restore', b'hello')[0] == 400)
                check('restore needs the web password', api('POST', '/api/restore', backup, auth=False)[0] == 401)

                # ONVIF and RTSP login.
                api('POST', '/api/wifi', 'ssid=auth-on')
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
                api('POST', '/api/wifi', 'ssid=auth-off')
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
            time.sleep(0.5)
            get(f'/sensor?{sid(3)}')
            waiter.join(9)
            check('a held poll is answered when something changes', held.get('took', 99) < 3 and 'Value="true"' in held['reply'][1], held.get('took'))
            get(f'/sensor?{sid(3)}&state=clear')
            time.sleep(0.3)
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
            get(f'/sensor?{sid(6)}&state=clear')
            check('live report is not marked delayed', json.loads(get('/events')[1])[1]['delayed'] is False)

            # Cached alerts: amber picture, one record each, true time kept in the event list.
            before = len(nvr.events)
            get(f'/sensor?{sid(4)}&age=100&direction=1&distance=250&battery=80')
            get(f'/sensor?{sid(5)}&age=unknown')
            time.sleep(0.5)
            now = status()
            check('first cached alert plays, second waits', now['active'] == [name(4)] and now['delayed_waiting'] == 1, now)
            check('delayed alert still reaches the NVR', 'true' in nvr.states()[before:], nvr.states()[before:])
            colour = grab_colour()
            check('missing direction picture falls back to the delayed one', colour == 'amber', colour)
            played = time.time()
            while status()['active'] != [name(5)] and time.time() - played < 20:
                time.sleep(0.2)
            check('second cached alert plays after the first', status()['active'] == [name(5)])
            time.sleep(1)  # the push for the second alert goes out just after it becomes active
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
            played = time.time()
            while status()['active'] and time.time() - played < 15:
                time.sleep(0.2)

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
            get(f'/sensor?{sid(1)}&state=clear')
            check('still answering at the end', soap('GetSystemDateAndTime', device)[0] == 200)

            # Deleting folders removes the sensors, without a reboot.
            count = status()['sensors']
            check('sensor folder can be deleted', api('DELETE', f'/api/file?path={name(6)}')[0] == 200)
            check('deleted sensor is unknown at once', get(f'/sensor?{sid(6)}')[0] == 404 and status()['sensors'] == count - 1)
            check('slave folder can be deleted with its contents', api('DELETE', f'/api/file?path={A}')[0] == 200)
            check('all its sensors are gone', get(f'/sensor?{sid(1)}')[0] == 404 and status()['sensors'] == count - 4)

            if proc:
                proc.kill()
                proc.wait()
                proc = subprocess.Popen([EXE, str(HOLD_MS), storage, PASSWORD], stdout=subprocess.PIPE, text=True)
                proc.stdout.readline()
                check('folders are still sensors after a restart', status()['sensors'] == count - 4, status())
                check('stored pictures survive a restart', api('GET', f'/api/file?path={name(4)}/active.h264')[1] == files['active.h264'])
            api('DELETE', f'/api/file?path={B}')
        finally:
            if proc:
                proc.kill()
                proc.wait()
    print(f'\n{len(failures)} failure(s)' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
