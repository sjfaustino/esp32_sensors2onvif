"""Takes the pictures of the hub's pages in docs/images, from the PC build with a few sensors and events.

Build the PC hub first with pio run -e host; the browser needs pip install playwright==1.52.0 and
python -m playwright install chromium. Run it again whenever a page changes, and commit the pictures.
"""

import os, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the parts beside this file
from e2e_common import *  # noqa: E402,F401,F403
import e2e_common as common  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

URL = f'http://{HOST}:{WEB}/'
OUT = os.path.join(ROOT, 'docs', 'images')
NAMES = {1: 'Front door', 2: 'Hall', 3: 'Kitchen window', 4: 'Garage', 5: 'Garden gate', 6: 'Back door'}
# The same language and time zone on every computer, so the dates and times the pages show take the same room and the
# pictures CI takes match those in docs/images (tools/compare_pictures.py).
SAME_EVERYWHERE = {'locale': 'en-GB', 'timezone_id': 'UTC'}
# The pages to take, with the element each must show first.
PAGES = [
    ('overview', '#view-overview'),
    ('sensors', '#view-sensors .slots'),
    ('events', '#view-events table'),
    ('walk', '#view-walk'),
    ('espnow', '#view-espnow table'),
    ('notify', '#view-notify'),
    ('nvr', '#view-nvr'),
    ('maintenance', '#view-maintenance'),
]


def fill_the_hub():
    """Sensors with names, a node heard over the radio, and some alerts in the history."""
    for n, label in NAMES.items():
        api('POST', f'/api/mkdir?path={name(n)}')
        api('PUT', f'/api/file?path={name(n)}/sensor.txt', f'name={label}\n'.encode())
    api('POST', f'/api/sensor-settings?path={name(1)}', 'camera=Camera%201%2C%20porch')
    api('POST', '/api/nvr', f'ip={LOCAL}')  # the pretend NVR, so the hub can check what it recorded
    control('radio-known')
    for n in (2, 1, 4, 6):
        get(f'/sensor?{sid(n)}&battery=82')
        clear(n)
        time.sleep(0.3)


# Just before each picture, the dates and times the page shows, which are those of the test's own alerts and differ on
# every run, are made the same fixed ones, so the pictures change only when a page does.
STEADY = r"""() => {
    const walk = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let n; (n = walk.nextNode()); )
        n.nodeValue = n.nodeValue.replace(/\b\d{1,2}\/\d{1,2}\/\d{4}\b/g, '22/11/2026')
            .replace(/\b\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\b/g, '2026-11-22 08:00:00')
            .replace(/\b\d{1,2}:\d{2}:\d{2}\b/g, '08:00:00').replace(/\b\d{1,2}:\d{2}\b/g, '08:00');
}"""


# No animation, so every machine draws the page in the same place whatever its speed.
STILL = '*, *::before, *::after { transition: none !important; animation: none !important; }'


def steady(page):
    page.evaluate(STEADY)


def take(page, view, ready, file):
    page.goto(URL + '#' + view)
    page.wait_for_selector(ready, state='visible')
    page.wait_for_timeout(800)  # the page fills its tables from the hub
    steady(page)
    page.screenshot(path=os.path.join(OUT, file + '.png'))
    print('took', file)


def main():
    if DEVICE:
        sys.exit('the pictures are taken from the PC build only')
    os.makedirs(OUT, exist_ok=True)
    with (tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as blank,
          tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage, sync_playwright() as p):
        browser = p.chromium.launch()
        try:
            # A blank hub's setup page, on a phone, as someone setting the hub up first sees it.
            start_hub(blank)
            assert common.HUB.stdout.readline().strip() == 'ready'
            control('setup-on')
            phone = browser.new_page(viewport={'width': 390, 'height': 760}, device_scale_factor=2, **SAME_EVERYWHERE)
            phone.goto(URL + 'setup')
            phone.wait_for_selector('#hubpw', state='visible')
            phone.wait_for_timeout(500)
            phone.screenshot(path=os.path.join(OUT, 'setup.png'))
            print('took setup')
            phone.close()
            common.HUB.kill()
            common.HUB.wait()

            start_hub(storage, PASSWORD)
            assert common.HUB.stdout.readline().strip() == 'ready'
            nvr = FakeNvrListener()
            inner = f'<ConsumerReference><Address>http://{LOCAL}:{nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
            soap('Subscribe', '/onvif/device_service', inner)
            fill_the_hub()
            context = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD}, **SAME_EVERYWHERE,
                                          viewport={'width': 1280, 'height': 820})
            context.route('https://api.github.com/**', lambda r: r.abort())  # no newer release offered: the same picture offline
            page = context.new_page()
            for view, ready in PAGES:
                take(page, view, ready, view.split(':')[0])
            # The Overview on a phone set to dark.
            dark = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD}, color_scheme='dark', **SAME_EVERYWHERE,
                                       viewport={'width': 390, 'height': 760}, device_scale_factor=2).new_page()
            dark.goto(URL + '#overview')
            dark.wait_for_selector('#wizard', state='visible')
            dark.add_style_tag(content=STILL)  # the menu's opening is not animated, so where it ends is where the page scrolls
            dark.wait_for_timeout(300)
            dark.evaluate("document.getElementById('menu').scrollIntoView(false); scrollTo(0, document.getElementById('status').offsetTop - 60)")
            dark.wait_for_timeout(800)
            steady(dark)
            dark.screenshot(path=os.path.join(OUT, 'phone-dark.png'))
            print('took phone-dark')
        finally:
            browser.close()
            if common.HUB:
                common.HUB.kill()
                common.HUB.wait()


if __name__ == '__main__':
    main()
