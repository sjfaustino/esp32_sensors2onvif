"""The file manager, pictures, backups and the web pages."""
from e2e_common import *  # noqa: F401,F403  (the helpers every part shares)
import e2e_common as common


def test_connections_cannot_be_held():
    """Anyone on the network could hold the hub's few connections, and with them keep the NVR out until its subscription
    lapsed and alerts stopped reaching it. A refused body trickled in is cut off, and the NVR always gets in."""
    # A refused upload whose body comes a byte a second: read and dropped only so long, here 3 s.
    control('drain-fast')
    s = socket.create_connection((HOST, WEB), timeout=10)
    s.sendall(f'PUT /api/firmware HTTP/1.1\r\nHost: {HOST}:{WEB}\r\nContent-Length: 1000000\r\n\r\n'.encode())
    closed_after = None
    started = time.time()
    try:
        s.settimeout(1)
        while time.time() - started < 9:
            try:
                s.sendall(b'x')
            except OSError:
                closed_after = time.time() - started
                break
            try:
                if s.recv(4096) == b'':
                    closed_after = time.time() - started
                    break
            except socket.timeout:
                pass
            except OSError:
                closed_after = time.time() - started
                break
    finally:
        s.close()
        control('drain-normal')
    check('a refused body trickled in is cut off', closed_after is not None and closed_after < 7, closed_after)

    # Every connection busy, from one address, and the NVR at another: it is let in, in place of the oldest.
    api('POST', '/api/nvr', 'ip=127.0.0.2')
    let_in = int(control('nvr-let-in'))
    held = []
    try:
        for _ in range(6):
            held.append(socket.create_connection((HOST, WEB), timeout=5))
        time.sleep(0.5)  # the hub takes them all
        try:
            code = soap('GetSystemDateAndTime', '/onvif/device_service', source='127.0.0.2')[0]
        except (OSError, http.client.HTTPException) as e:
            code = str(e)
        check('the NVR gets in while every connection is busy', code == 200 and int(control('nvr-let-in')) == let_in + 1, code)
    finally:
        for h in held:
            h.close()

    # Both video connections held by someone asking without the password: the NVR's video still gets in.
    video_in = int(control('nvr-video-let-in'))
    viewers = []
    try:
        for _ in range(2):
            v = socket.create_connection((HOST, RTSP), timeout=5)
            v.sendall(f'OPTIONS rtsp://{HOST}:{RTSP}/stream RTSP/1.0\r\nCSeq: 1\r\n\r\n'.encode())
            v.recv(4096)
            viewers.append(v)
        nvr = socket.create_connection((HOST, RTSP), timeout=5, source_address=('127.0.0.2', 0))
        try:
            nvr.sendall(f'OPTIONS rtsp://{HOST}:{RTSP}/stream RTSP/1.0\r\nCSeq: 1\r\n\r\n'.encode())
            answer = nvr.recv(4096).decode(errors='replace')
        except OSError as e:
            answer = str(e)
        finally:
            nvr.close()
        check("the NVR's video gets in while both video connections are held", answer.startswith('RTSP/1.0 200')
              and int(control('nvr-video-let-in')) == video_in + 1, answer[:60])
    finally:
        for v in viewers:
            v.close()
        api('POST', '/api/nvr', 'ip=')


def floor_plan_tests():
    """The floor plan's two files at the top of storage: a JPEG, and where each sensor is."""
    if DEVICE:  # never on a real hub, which may have its own plan
        return
    jpeg = bytes([0xFF, 0xD8, 0xFF, 0xE0]) + bytes(3000) + bytes([0xFF, 0xD9])
    check('the floor plan picture is saved', api('PUT', '/api/file?path=plan.jpg', jpeg)[0] == 200)
    got = api('GET', '/api/file?path=plan.jpg')
    check('and read back as it was', got[0] == 200 and got[1] == jpeg)
    check('a floor plan that is not a JPEG is refused', api('PUT', '/api/file?path=plan.jpg', b'PNG' + bytes(100))[0] == 422)
    places = json.dumps({f'02-00-00-00-{n:02X}-01/{i}': [999, 999] for n in range(16) for i in (1, 2)})  # 32 sensors
    check('places for 32 sensors fit', len(places) > 1024 and api('PUT', '/api/file?path=plan.json', places)[0] == 200, len(places))
    check('more than 2 KB of places is refused', api('PUT', '/api/file?path=plan.json', 'x' * 2100)[0] == 413)
    check('a plan of another kind of file is refused', api('PUT', '/api/file?path=plan.png', jpeg)[0] == 400)
    check('the floor plan can be deleted', api('DELETE', '/api/file?path=plan.jpg')[0] == 200 and api('DELETE', '/api/file?path=plan.json')[0] == 200
          and api('GET', '/api/file?path=plan.jpg')[0] == 404)


def file_manager_tests(files, hub_files_free):
    floor_plan_tests()
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
    check("the hub\'s own page is not refused as another site", api('POST', '/api/restore', b'hello', origin=own_site)[0] == 400)

    code, page = api('GET', '/')
    check('page is served', code == 200 and b'<title>Alarm hub</title>' in page, code)
    check('page is sent gzipped', common.last_encoding == 'gzip', common.last_encoding)
    check('a browser must check for a newer page', common.last_cache == 'no-cache', common.last_cache)
    if not DEVICE:
        # Another site pointing its own name at the hub is refused; the hub's own names are not.
        check("a page under another site\'s name is refused", raw_get('/api/info', host='hub.attacker.example', password=PASSWORD) == 421)
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
    check('page offers to show a picture', b'"Show it"' in page)
    check('page has the menu',all(t in page for t in [b'>Network</div>', b'>Sensors</div>', b'data-view="network"', b'data-view="wifi"', b'data-view="onvif"', b'data-view="http"', b'data-view="rtsp"', b'data-view="sensors"',
                                                  b'data-view="espnow"', b'data-view="events"', b'>System</div>', b'data-view="maintenance"', b'data-view="time"', b'data-view="device"', b'>Recording</div>', b'data-view="nvr"', b'data-view="live">Live video<']))
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
    check('an unsupported ONVIF request is recorded', 'NoSuchRequest' in get_json('/api/info')['unsupported'])
    check('setting the time over ONVIF is refused, because the hub would ignore it', soap('SetSystemDateAndTime', '/onvif/device_service')[0] == 400)

    code, device = api('GET', '/api/device')
    rows = dict(json.loads(device)['rows']) if code == 200 else {}
    check('device info is served', 'Firmware version' in rows and 'Picture storage' in rows and 'Sensors' in rows, rows)
    check('time settings are served', api('GET', '/api/time')[0] == 200 and 'local' in get_json('/api/time'))
    code, sched = api('GET', '/api/schedule')
    check('restart schedule is served', code == 200 and 'minute' in json.loads(sched), code)
    check('device info needs the web password', not PASSWORD or api('GET', '/api/device', auth=False)[0] == 401)

    if not DEVICE:  # the PC build has a pretend crash report; a board's real one is not deleted
        check('device info says a crash report is saved', json.loads(device).get('crash') == 3000, json.loads(device).get('crash'))
        code, dump = api('GET', '/api/crash')
        check('the crash report downloads whole, read in pieces', code == 200 and dump == bytes(i % 251 for i in range(3000)), (code, len(dump)))
        check('the crash report needs the web password', not PASSWORD or api('GET', '/api/crash', auth=False)[0] == 401)
        check('the crash report can be deleted', api('POST', '/api/crash/delete', 'x=1')[0] == 200 and get_json('/api/device').get('crash') == 0)
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
        saved = get_json('/api/schedule')
        check('restart schedule reads back', saved == {'on': True, 'day': 3, 'minute': 240}, saved)
        for bad in ('on=1&day=7&minute=0', 'on=1&day=-2&minute=0', 'on=1&day=0&minute=1440', 'on=1&day=0&minute=x', 'on=1'):
            check(f'restart schedule is refused: {bad}', api('POST', '/api/schedule', bad)[0] == 400)
        check('restart needs the web password', api('POST', '/api/reboot', '', auth=False)[0] == 401)
        check('restart request is accepted', api('POST', '/api/reboot', '')[0] == 200)
        check('new keys must be confirmed', api('POST', '/api/newkeys', '')[0] == 400)
        check('new keys need the web password', api('POST', '/api/newkeys', 'confirm=yes', auth=False)[0] == 401)
        check('confirmed new keys are accepted', api('POST', '/api/newkeys', 'confirm=yes')[0] == 200)
        check('reset must be confirmed', api('POST', '/api/reset', '')[0] == 400)
        check('reset needs the web password', api('POST', '/api/reset', 'confirm=yes', auth=False)[0] == 401)
        check('confirmed reset is accepted', api('POST', '/api/reset', 'confirm=yes')[0] == 200)

        if not DEVICE:  # every route of the API, from the hub's own table, refuses a request without the login
            listed = [line.split(' ') for line in control('routes').split(';')]
            open_ones = [f'{m} {p}' for m, p, access in listed
                         if api('GET' if m == '*' else m, p, b'' if m != 'GET' else None, auth=False)[0] != 401]
            check(f'all {len(listed)} routes need the login', len(listed) > 40 and not open_ones, open_ones)
            check('the setup routes are marked as such', sorted(p for _, p, a in listed if a == 'setup') == ['/', '/api/wifi', '/api/wifi/scan', '/setup'],
                  [l for l in listed if l[2] == 'setup'])
            check('an unknown route needs the login before it is called unknown', api('GET', '/api/nothing-here', auth=False)[0] == 401
                  and api('GET', '/api/nothing-here')[0] == 404)
        if not DEVICE:
            test_connections_cannot_be_held()
        # Never framed by another site, which could lead someone to click Disarm unseen; the Wi-Fi page's own frame stays.
        for path in ('/', '/setup'):
            c = http.client.HTTPConnection(HOST, WEB, timeout=5)
            c.request('GET', path, headers={'Authorization': 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()} if PASSWORD else {})
            r = c.getresponse()
            r.read()
            c.close()
            check(f'{path} may be framed only by the hub itself', r.getheader('X-Frame-Options') == 'SAMEORIGIN'
                  and r.getheader('Content-Security-Policy') == "frame-ancestors 'self'" and r.getheader('X-Content-Type-Options') == 'nosniff',
                  (r.getheader('X-Frame-Options'), r.getheader('Content-Security-Policy')))
        c = http.client.HTTPConnection(HOST, WEB, timeout=5)
        c.request('GET', '/api/backup', headers={'Authorization': 'Basic ' + base64.b64encode(f'admin:{PASSWORD}'.encode()).decode()} if PASSWORD else {})
        r = c.getresponse()
        r.read()
        c.close()
        check('the settings backup, with its passwords, is never cached', r.status == 200 and r.getheader('Cache-Control') == 'no-store', r.getheader('Cache-Control'))
        code, page = api('GET', '/setup', auth=False)
        check('setup page needs the web password on the home network', code == 401, code)
        code, page = api('GET', '/setup')
        check('setup page is served', code == 200 and b'Hub Wi-Fi setup' in page and b'async function scan' in page, code)
        check('network list is served', b'test-net' in api('GET', '/api/wifi/scan')[1])
        if not DEVICE:
            names = [n['ssid'] for n in get_json('/api/wifi/scan')['networks']]
            check('a network name with quotes, a backslash and control characters arrives exactly', names == ['test-net', 'say "hi" \\ to\tall\x01'], names)
        check('a hub with a password does not ask for one on the setup page', get_json('/api/wifi/scan').get('needPassword') is False)
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
        check('file manager is back after setup', b'<title>Alarm hub</title>' in api('GET', '/')[1])
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
        nvr_info = get_json('/api/nvr')
        check('NVR address reads back', nvr_info['ip'] == '192.0.2.7', nvr_info)
        check('NVR not connected is reported', nvr_info['video'] is False and nvr_info['events'] is False, nvr_info)
        for bad in ('ip=1.2.3', 'ip=999.1.1.1', 'ip=abc'):
            check(f'NVR address is refused: {bad}', api('POST', '/api/nvr', bad)[0] == 400)
        check('NVR address can be cleared', api('POST', '/api/nvr', 'ip=')[0] == 200 and get_json('/api/nvr')['ip'] == '')
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
        def with_login(password, method, path, body=None, host=None):
            c = http.client.HTTPConnection(HOST, WEB, timeout=5)
            headers = {'Authorization': 'Basic ' + base64.b64encode(f'admin:{password}'.encode()).decode()}
            if host:
                headers['Host'] = host
            c.request(method, path, body, headers)
            r = c.getresponse()
            data = r.read()
            c.close()
            return r.status, data

        # A port or the name restarts the hub, as every service listens on or announces them; the PC build only says so.
        for good in ('onvif=9000', 'rtsp=9554', 'name=my-hub-2'):
            code, body = api('POST', '/api/settings', good)
            check(f'setting is accepted, and restarts the hub: {good}', code == 200 and json.loads(body).get('restart') is True, (code, body))
        code, body = api('POST', '/api/settings', 'protect=on&password=')
        check('saving with the password left blank keeps it, with no restart', code == 200 and json.loads(body).get('restart') is False, (code, body))
        if not DEVICE:  # a board's password is the tester's own
            code, body = api('POST', '/api/settings', 'password=8%20chars!')
            check('a new password is accepted without a restart', code == 200 and json.loads(body).get('restart') is False, (code, body))
            check('and the old one no longer opens the pages', api('GET', '/api/tree')[0] == 401)
            check('the new one does, at once', with_login('8 chars!', 'GET', '/api/tree')[0] == 200)
            code = with_login('8 chars!', 'POST', '/api/settings', 'password=' + urllib.parse.quote(PASSWORD))[0]
            check('and the test password can be put back', code == 200 and api('GET', '/api/tree')[0] == 200, code)
            control('auth-off')  # it guards ONVIF too, as on a board; the PC build's other tests run ONVIF without a login
        too_long = 'x' * 64
        for bad in ('web=0', 'web=70000', 'web=abc', 'onvif=80x', f'onvif={RTSP}', f'web={ONVIF}', f'onvif={WEB}', 'rtsp=0', 'rtsp=abc', f'rtsp={WEB}', f'rtsp={ONVIF}',
                    'name=-hub', 'name=hub-', 'name=a_b', 'name=a%20b', f'name={too_long}', f'protect=on&password={too_long}',
                    'protect=off', 'protect=off&password=new%20pass', 'password=7chars!'):
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
        check("a picture that would take the event history\'s room is refused", api('PUT', f'/api/file?path={path}', good)[0] == 507)
        check('the Sensors page says how much is kept for the history', get_json('/api/tree')['kept'] > 512 * 1024 - 1)
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
    listed = [f for s in get_json('/api/tree')['nodes'] for n in s['sensors'] for f in n['files'] if f['name'] == 'approaching.h264']
    check('tree says a picture has a thumbnail', any(f['preview'] for f in listed), listed)
    check('tree does not list thumbnails as pictures', not [f for s in get_json('/api/tree')['nodes'] for n in s['sensors'] for f in n['files'] if f['name'].endswith('.jpg')])
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

    tree = get_json('/api/tree')
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
    check('whole page arrives', full[1].rstrip().endswith(b'</html>') and b'Make and save 10 pictures' in full[1] and b'Make it from an image' in full[1], len(full[1]))

    check('picture can be deleted', api('DELETE', f'/api/file?path={path}')[0] == 200)
    check('deleted picture is gone', api('GET', f'/api/file?path={path}')[0] == 404)
    # Only these two exist for sensor 4, so a report with a direction has to fall back to them.
    for f in ('active.h264', 'active-delayed.h264'):
        api('PUT', f'/api/file?path={name(4)}/{f}', files[f])
