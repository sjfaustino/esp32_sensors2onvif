"""Updates a hub over the network, from this checkout, and watches that it keeps the new firmware.

    python tools/update_hub.py 192.168.1.50            # build, sign, check, send, and watch it for 5 minutes
    python tools/update_hub.py 192.168.1.50 --watch 10 # watch it for 10 minutes
    python tools/update_hub.py 192.168.1.50 --untested # a build CI has not passed, such as while finding a fault
    python tools/update_hub.py 192.168.1.50 --release v0.6.0  # a published release, as CI built and signed it

It refuses, before sending anything:
  - a build with changes not committed, or a commit not pushed, as nobody could tell later what the hub runs;
  - a commit whose CI checks have not passed, unless --untested: the PC tests cannot show the board's limits, and CI
    is the least a build must pass before it goes on a hub that guards a home;
  - while an alert is active, as an update pauses the video the NVR records. Alerts waiting their turn, as while the
    NVR is away, do not stop it: the hub keeps them in flash and replays them after the restart, as after any other.

Then it builds the 16 MB board's firmware (--env for another), signs it with the project's key (tools/sign_firmware.py),
sends it to the hub's /api/firmware, and waits for it to come back with the new version. A new firmware is only on
trial: the hub goes back to the previous one by itself if it crashes or cannot reach what it needs. So this watches
until the hub says its firmware check is Good and it has run for --watch minutes without restarting, and says plainly
if it went back. The hub's password is read from HUB_PASSWORD, or else include/secrets.h, and never printed.
"""
import argparse
import base64
import hashlib
import http.client
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding='utf-8', errors='replace')


def fail(text):
    print('refused: ' + text if not text.startswith('FAILED') else text)
    sys.exit(1)


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def password():
    if os.environ.get('HUB_PASSWORD'):
        return os.environ['HUB_PASSWORD']
    try:
        text = open(os.path.join(ROOT, 'include', 'secrets.h'), encoding='utf-8').read()
    except OSError:
        fail('no HUB_PASSWORD, and no include/secrets.h to read it from')
    for name in ('WEB_PASSWORD', 'SETUP_AP_PASSWORD'):  # the web password, or the setup one the hub starts with
        m = re.search(r'^\s*#define\s+' + name + r'\s+"([^"]*)"', text, re.M)
        if m and m.group(1):
            return m.group(1)
    fail('no password in HUB_PASSWORD or include/secrets.h')


class Hub:
    def __init__(self, host, secret):
        self.host = host
        self.auth = {'Authorization': 'Basic ' + base64.b64encode(f'admin:{secret}'.encode()).decode()}

    def request(self, method, path, body=None, timeout=10):
        c = http.client.HTTPConnection(self.host, 80, timeout=timeout)
        headers = dict(self.auth)
        if body is not None:
            headers['Content-Length'] = str(len(body))
        c.request(method, path, body, headers)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, data

    def json(self, path):
        status, data = self.request('GET', path)
        if status == 401:
            fail('the hub refused the password')
        if status != 200:
            raise OSError(f'{path} answered {status}')
        return json.loads(data)

    def device(self):
        return dict(self.json('/api/device')['rows'])

    def status(self):
        return self.json('/status')


def check_commit(untested):
    if git('status', '--porcelain', '--untracked-files=no'):
        if not untested:
            fail('this checkout has changes not committed; commit them, or add --untested')
        print('note: building with changes not committed (--untested)')
    sha = git('rev-parse', 'HEAD')
    if not git('branch', '-r', '--contains', sha):
        if not untested:
            fail(f'commit {sha[:7]} is not pushed, so CI has not checked it; push it, or add --untested')
        return sha
    if untested:
        print('note: CI is not consulted (--untested)')
        return sha
    gh = shutil.which('gh')
    if not gh:
        fail("the GitHub CLI (gh) is needed to see CI's verdict; install it, or add --untested")
    out = subprocess.run([gh, 'run', 'list', '--commit', sha, '--workflow', 'Checks', '--json', 'status,conclusion,url',
                          '--limit', '1'], cwd=ROOT, capture_output=True, text=True)
    runs = json.loads(out.stdout or '[]') if out.returncode == 0 else None
    if runs is None:
        fail("could not ask GitHub for CI's verdict: " + out.stderr.strip())
    if not runs:
        fail(f'CI has no run for {sha[:7]} yet')
    run = runs[0]
    if run['status'] != 'completed':
        fail(f'CI is still checking {sha[:7]}: {run["url"]}')
    if run['conclusion'] != 'success':
        fail(f'CI failed {sha[:7]} ({run["conclusion"]}): {run["url"]}')
    print(f'CI passed {sha[:7]}')
    return sha


def built_version():
    text = open(os.path.join(ROOT, 'include', 'version.h'), encoding='utf-8').read()
    return re.search(r'#define FIRMWARE_VERSION "([^"]*)"', text).group(1)


def built(env):
    """This checkout's firmware, built and signed here."""
    print(f'-- building {env}', flush=True)
    if subprocess.run(['pio', 'run', '-e', env], cwd=ROOT, capture_output=True).returncode:
        fail(f'the build failed: pio run -e {env} says why')
    with tempfile.TemporaryDirectory() as scratch:
        signed = os.path.join(scratch, 'firmware-signed.bin')
        image = os.path.join(ROOT, '.pio', 'build', env, 'firmware.bin')
        if subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'sign_firmware.py'), image, signed],
                          capture_output=True).returncode:
            fail('signing failed: is the key in ~/.sensors2onvif/firmware-signing-key.pem?')
        return built_version(), open(signed, 'rb').read()


def released(tag, env):
    """A published release's firmware for the board, as CI built and signed it, checked against its SHA256SUMS."""
    gh = shutil.which('gh')
    if not gh:
        fail('the GitHub CLI (gh) is needed to download a release')
    name = f'sensors2onvif-{tag}-{env}.bin'
    with tempfile.TemporaryDirectory() as scratch:
        got = subprocess.run([gh, 'release', 'download', tag, '--pattern', name, '--pattern', 'SHA256SUMS', '--dir', scratch],
                             cwd=ROOT, capture_output=True, text=True)
        if got.returncode:
            fail(f'could not download {name} from the release {tag}: {got.stderr.strip()}')
        data = open(os.path.join(scratch, name), 'rb').read()
        sums = open(os.path.join(scratch, 'SHA256SUMS'), encoding='utf-8').read()
    if f'{hashlib.sha256(data).hexdigest()}  {name}' not in sums:
        fail(f"{name} does not match the release's SHA256SUMS")
    print(f"-- downloaded {name}, matching the release's SHA256SUMS", flush=True)
    return tag, data


def health_line(hub):
    s = hub.status()
    o = hub.json('/api/overview')
    nvr = o.get('nvr') or {}
    return (f"{'armed ' + s['mode'] if s.get('armed') else 'DISARMED'}; NVR "
            f"{'listening' if nvr.get('listening') else 'NOT listening'}; {len(s.get('active', []))} alerts active; "
            f"problems: {', '.join(p if isinstance(p, str) else json.dumps(p) for p in o.get('problems') or []) or 'none'}")


def main():
    parser = argparse.ArgumentParser(description='Update a hub over the network and watch that it keeps the firmware.')
    parser.add_argument('host', help="the hub's address, such as 192.168.1.50")
    parser.add_argument('--env', default='esp32_16mb', help="the board's PlatformIO environment (default esp32_16mb)")
    parser.add_argument('--watch', type=float, default=5, help='minutes it must run without restarting (default 5)')
    parser.add_argument('--untested', action='store_true', help="send a build CI has not passed, or not committed")
    parser.add_argument('--release', metavar='TAG', help='send a published release, such as v0.6.0, instead of a build')
    args = parser.parse_args()

    hub = Hub(args.host, password())
    if not args.release:  # a release was built by CI from a tag that passed it
        check_commit(args.untested)
    try:
        before = hub.device()
        s = hub.status()
    except OSError as e:
        fail(f'the hub at {args.host} did not answer: {e}')
    if s.get('active'):
        fail('an alert is active; an update would pause the video the NVR records. Try again once it is over')
    if s.get('delayed_waiting'):
        print(f"note: {s['delayed_waiting']} alert(s) wait for the NVR; the hub keeps them and replays them after the update")
    print(f"the hub runs {before.get('Firmware version')}; {health_line(hub)}")

    version, data = released(args.release, args.env) if args.release else built(args.env)
    print(f'-- sending {version}, {len(data) // 1024} KB', flush=True)
    status, reply = hub.request('PUT', '/api/firmware', data, timeout=180)
    if status != 200:
        fail(f'the hub refused it: {status} {reply[:200].decode(errors="replace")}')

    print('-- waiting for it to restart into the new firmware', flush=True)
    deadline, now_runs = time.time() + 120, None
    while time.time() < deadline:
        time.sleep(3)
        try:
            now_runs = hub.device().get('Firmware version')
            break
        except (OSError, ValueError):
            pass
    if now_runs is None:
        fail('FAILED: the hub did not come back within 2 minutes. Check its power and its light; it goes back to the '
             'previous firmware by itself if the new one cannot start')
    if now_runs != version:
        fail(f'FAILED: the hub came back running {now_runs}, not {version}: it refused the new firmware or went back')

    print(f'-- {version} runs; watching it for {args.watch:g} minutes', flush=True)
    # Timed by the hub's own uptime and by tries, not by this PC's clock, which runs on while the PC sleeps: a PC that
    # slept through the watch must not call a firmware that proved itself meanwhile a failure.
    limit = max(args.watch * 60, 600) + 300
    last_uptime, silent = -1, 0
    while True:
        time.sleep(10)
        try:
            d = hub.device()
            uptime = hub.status()['uptime_s']
            silent = 0
        except (OSError, ValueError, KeyError):
            silent += 1
            if silent >= 18:  # a moment's silence over Wi-Fi is no verdict; 18 tries, three minutes, is
                fail('FAILED: the hub stopped answering for 3 minutes. Check its power and its light; if the new firmware '
                     'cannot start, it goes back to the previous one by itself')
            continue
        if d.get('Firmware version') != version:
            fail(f'FAILED: the hub went back to {d.get("Firmware version")}: the new firmware failed its trial. '
                 f'Last restart: {d.get("Last restart", "not known")}')
        if uptime < last_uptime:
            fail(f'FAILED: the hub restarted while being watched. Last restart: {d.get("Last restart", "not known")}')
        last_uptime = uptime
        proved = d.get('Firmware check') == 'Good'
        if proved and uptime >= args.watch * 60:
            break
        if not proved and uptime > limit:
            fail(f'FAILED: after {uptime // 60} minutes running, the firmware check still says "{d.get("Firmware check")}"')
    print(f"kept: {version}, its check Good, {uptime} s without a restart; free memory {d.get('Free memory')}, "
          f"lowest {d.get('Lowest free memory')}")
    print(health_line(hub))


if __name__ == '__main__':
    main()
