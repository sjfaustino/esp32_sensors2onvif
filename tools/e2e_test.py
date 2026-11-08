"""End-to-end test of the firmware library, built for the PC (pio run -e host).

Plays the NVR's part on 127.0.0.1 using the call order recorded from the real NVR. Every socket
here is loopback, so nothing reaches any NVR or camera. Needs ffmpeg.
"""
import http.client, os, re, socket, subprocess, sys, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, '.pio', 'build', 'host', 'program.exe')
HOST, ONVIF, RTSP, DISCOVERY = '127.0.0.1', 18899, 18554, 13702
HOLD_MS = 8000
failures = []


def check(name, ok, detail=''):
    print(('PASS ' if ok else 'FAIL ') + name + (f'  [{detail}]' if detail and not ok else ''), flush=True)
    if not ok:
        failures.append(name)


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
    c = http.client.HTTPConnection(HOST, ONVIF, timeout=5)
    c.request('GET', path)
    r = c.getresponse()
    data = r.read().decode()
    c.close()
    return r.status, data


class FakeNvrListener:
    """Records each Notify and, like the real NVR, closes without an HTTP reply."""

    def __init__(self):
        self.sock = socket.socket()
        self.sock.bind((HOST, 0))
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
    return 'red' if r > g else 'green'


def main():
    proc = subprocess.Popen([EXE, str(HOLD_MS)], stdout=subprocess.PIPE, text=True)
    try:
        check('server starts', proc.stdout.readline().strip() == 'ready')

        # The order the NVR used when first connecting.
        device, media = '/onvif/device_service', '/onvif/media'
        for action, path in [('GetSystemDateAndTime', device), ('GetProfiles', media), ('GetStreamUri', media), ('GetCapabilities', device),
                             ('GetDeviceInformation', device), ('SetSynchronizationPoint', media), ('GetAudioSources', media),
                             ('GetVideoEncoderConfiguration', media), ('GetVideoEncoderConfigurationOptions', media)]:
            status, data = soap(action, path)
            check(f'{action} answered', status == 200 and f'{action}Response' in data, data[:200])
        status, data = soap('GetStreamUri')
        check('stream URI follows the Host header', f'rtsp://{HOST}:{RTSP}/stream' in data, data[:300])
        status, _ = soap('NoSuchCall')
        check('unknown call is refused', status == 400)

        # The NVR sends Subscribe to device_service, twice, with one listener URL.
        nvr = FakeNvrListener()
        inner = f'<ConsumerReference><Address>http://{HOST}:{nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
        for _ in range(2):
            status, data = soap('Subscribe', device, inner)
            check('Subscribe accepted on device_service', status == 200 and 'SubscribeResponse' in data, data[:200])
        check('two identical Subscribes make one subscriber', '"subscribers":1' in get('/status')[1])
        time.sleep(0.7)
        check('initial state is pushed as false', nvr.states()[:1] == ['false'], nvr.states())
        check('initial push is marked Initialized', 'PropertyOperation="Initialized"' in nvr.events[0][1])
        check('each Subscribe gets its initial state', len(nvr.events) == 2, len(nvr.events))

        colour = grab_colour()
        check('picture is green when clear', colour == 'green', colour)

        # A sensor alert makes a motion event, and the picture shows it.
        status, _ = get('/sensor?id=99')
        check('unknown sensor is rejected', status == 404)
        before = len(nvr.events)
        started = time.time()
        status, data = get('/sensor?id=3')
        check('sensor alert accepted', status == 200 and '"ok":true' in data, data)
        time.sleep(0.6)
        check('alert reaches the NVR quickly', nvr.states()[before:before + 1] == ['true'], nvr.states())
        check('one notification per event despite two Subscribes', len(nvr.events) == before + 1, len(nvr.events) - before)
        pushed = nvr.events[before][1] if len(nvr.events) > before else ''
        check('push has both motion topics', 'VideoSource/MotionAlarm' in pushed and 'CellMotionDetector/Motion' in pushed)
        colour = grab_colour()
        check('picture is red during the alert', colour == 'red', colour)

        # Repeated "true" while active, then "false" when the hold ends. The grab above took some of the hold.
        remaining = HOLD_MS / 1000 + 1.5 - (time.time() - started)
        time.sleep(max(remaining, 0))
        during = [e for e in nvr.events[before:] if time.time() - e[0] < 1e6]
        states = [re.search(r'Name="State" Value="(\w+)"', e[1]).group(1) for e in during]
        check('true repeats while active', states.count('true') >= 6, states)
        trues = [e[0] for e, s in zip(during, states) if s == 'true']
        gaps = [b - a for a, b in zip(trues, trues[1:])]
        check('repeat gap stays under the 2 s latch', bool(gaps) and max(gaps) < 1.9, gaps)
        check('false is sent when the hold ends', states[-1] == 'false', states)
        colour = grab_colour()
        check('picture is green again', colour == 'green', colour)

        # Clearing early.
        before = len(nvr.events)
        get('/sensor?id=2')
        time.sleep(0.5)
        status, _ = get('/sensor?id=2&state=clear')
        check('sensor can be cleared', status == 200)
        time.sleep(0.5)
        check('clear sends false at once', nvr.states()[before:][-1:] == ['false'], nvr.states()[before:])

        # Other clients may poll instead.
        status, data = soap('CreatePullPointSubscription', '/onvif/events')
        address = re.search(r'/onvif/sub/\d+', data).group(0)
        status, data = soap('PullMessages', address)
        check('pull point delivers the state once', 'Value="false"' in data)
        status, data = soap('PullMessages', address)
        check('pull point is quiet until a change', 'NotificationMessage' not in data)

        # WS-Discovery, sent directly over loopback.
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.settimeout(3)
        probe = ('<e:Envelope xmlns:e="http://www.w3.org/2003/05/soap-envelope" xmlns:w="a" xmlns:d="b" xmlns:dn="c"><e:Header><w:MessageID>uuid:test-1</w:MessageID>'
                 '</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body></e:Envelope>')
        udp.sendto(probe.encode(), (HOST, DISCOVERY))
        reply = udp.recv(4096).decode()
        check('discovery answers a Probe', 'ProbeMatch' in reply and 'uuid:test-1' in reply and f'http://{HOST}:{ONVIF}/onvif/device_service' in reply, reply[:300])

        # A listener that accepts and never answers must not stall the camera or the good subscriber.
        hung = socket.socket()
        hung.bind((HOST, 0))
        hung.listen(8)
        soap('Subscribe', device, f'<ConsumerReference><Address>http://{HOST}:{hung.getsockname()[1]}/x</Address></ConsumerReference>')
        before = len(nvr.events)
        get('/sensor?id=1')
        slowest = 0
        for _ in range(15):
            t = time.time()
            status = soap('GetSystemDateAndTime', device)[0]
            slowest = max(slowest, time.time() - t)
            time.sleep(0.1)
        check('camera keeps answering with a hung listener', status == 200 and slowest < 0.5, slowest)
        check('good subscriber still gets events with a hung one present', 'true' in nvr.states()[before:], nvr.states()[before:])
        get('/sensor?id=1&state=clear')
        check('still answering at the end', soap('GetSystemDateAndTime', device)[0] == 200)
    finally:
        proc.kill()
    print(f'\n{len(failures)} failure(s)' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
