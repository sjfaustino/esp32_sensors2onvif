"""End-to-end test of the firmware library, built for the PC (pio run -e host).

Plays the NVR's part using the call order recorded from the real NVR. By default every socket is
loopback. Needs ffmpeg.

To test a flashed board instead, restart it first, then set E2E_DEVICE to its address and
E2E_LOCAL_IP to this PC's address on the same network. If the board has a web password, set
E2E_WEB_PASSWORD too. It creates and deletes only the 02-00-00-00-00-0x test folders. If the board
has its own clear.h264 or text.json it skips the checks that would replace them, and it stops
if it cannot tell. Never point E2E_DEVICE at an NVR.
"""
import base64, datetime, http.client, json, os, re, socket, subprocess, sys, tempfile, threading, time

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


def soap(action, path='/onvif/media', inner=''):
    body = ('<?xml version="1.0"?><s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"><s:Body>'
            f'<{action} xmlns="x">{inner}</{action}></s:Body></s:Envelope>')
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('POST', path, body, {'Content-Type': 'application/soap+xml', 'Host': f'{HOST}:{ONVIF}'})
    r = c.getresponse()
    data = r.read().decode()
    c.close()
    return r.status, data


def get(path):
    c = http.client.HTTPConnection(HOST, WEB, timeout=5)
    c.request('GET', path)
    r = c.getresponse()
    data = r.read().decode()
    c.close()
    return r.status, data


def api(method, path, body=None, auth=True):
    """Returns (status, body bytes)."""
    headers = {}
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
    out = subprocess.run(['ffmpeg', '-v', 'error', '-rtsp_transport', 'tcp', '-i', f'rtsp://{HOST}:{RTSP}/stream', '-frames:v', '1',
                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], capture_output=True, timeout=40)
    if len(out.stdout) != 640 * 360 * 3:
        return 'no frame: ' + out.stderr.decode(errors='replace')[:300]
    r, g = sum(out.stdout[0::3]), sum(out.stdout[1::3])
    if g > r:
        return 'green'
    return 'amber' if g > 0.3 * r else 'red'


def file_manager_tests(files, hub_files_free):
    if PASSWORD:
        check('file manager needs the password', api('GET', '/', auth=False)[0] == 401)
        check('file operations need the password', api('DELETE', f'/api/file?path={A}', auth=False)[0] == 401)
    for slave in (A, B):
        api('DELETE', f'/api/file?path={slave}')
    baseline = status()['sensors']

    code, page = api('GET', '/')
    check('page is served', code == 200 and b'SensorCam files' in page, code)
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

    if not DEVICE:  # saving Wi-Fi on a real board would move it to another network
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
    for size in (8193, 20000, 33792, 60000):
        check(f'oversize upload ({size} bytes) gets a clear refusal', api('PUT', f'/api/file?path={path}', b'\x55' * size)[0] == 413)
    largest = good + b'\x00' * (8192 - len(good))
    check('picture at the 8 KB limit is accepted', api('PUT', f'/api/file?path={path}', largest)[0] == 200)
    check('picture at the limit reads back', api('GET', f'/api/file?path={path}')[1] == largest)
    check('smaller picture replaces it', api('PUT', f'/api/file?path={path}', good)[0] == 200)
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
    check('tree shows the file and its size', sensors_b[1]['files'] == [{'name': 'approaching.h264', 'size': len(good)}], sensors_b[1])

    words = json.dumps({'label': 'FRONT DOOR', 'active': 'SOMEONE THERE'}).encode()
    text = f'{name(4)}/text.json'
    check('sensor text can be saved', api('PUT', f'/api/file?path={text}', words)[0] == 200)
    check('sensor text reads back', api('GET', f'/api/file?path={text}') == (200, words))
    check('oversize text is refused', api('PUT', f'/api/file?path={text}', b'x' * 1025)[0] == 413)
    check('text file is not listed as a picture', sensors_b[1]['files'] == [{'name': 'approaching.h264', 'size': len(good)}])
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

            # Other clients may poll instead.
            code, data = soap('CreatePullPointSubscription', '/onvif/events')
            address = re.search(r'/onvif/sub/\d+', data).group(0)
            check('pull point delivers the state once', 'Value="false"' in soap('PullMessages', address)[1])
            check('pull point is quiet until a change', 'NotificationMessage' not in soap('PullMessages', address)[1])

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
