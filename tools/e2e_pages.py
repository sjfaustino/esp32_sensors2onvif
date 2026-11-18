"""The web pages in a real browser, against the PC build (pio run -e host): the controls that decide whether an intruder
is recorded. Arming and disarming on the Overview page, and the test alert on the Sensors page and a sensor's own page,
each checked on the page and on the hub, and a pretend NVR that must get the test alerts. The API itself is tested by
e2e_test.py; this checks that the pages use it rightly, and never show the hub armed when it is not.

Needs Playwright and its Chromium:
    pip install playwright==1.52.0
    python -m playwright install chromium
"""
import os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # the parts beside this file
from e2e_common import *  # noqa: E402,F401,F403
import e2e_common as common  # noqa: E402
from playwright.sync_api import sync_playwright  # noqa: E402

URL = f'http://{HOST}:{WEB}/'
SENSOR = f'{A}/1'


class Dialogs:
    """Every alert and confirm the page shows, answered as the test sets: accepted, or cancelled."""

    def __init__(self, page):
        self.seen = []
        self.accept = True
        page.on('dialog', self.answer)

    def answer(self, d):
        self.seen.append((d.type, d.message))
        d.accept() if self.accept else d.dismiss()

    def last(self):
        return self.seen[-1] if self.seen else ('', '')


def page_wait(page, condition, timeout=5):
    """Waits through the page, so that its dialogs and other events arrive meanwhile, until condition is true."""
    for _ in range(int(timeout * 20)):
        if condition():
            return True
        page.wait_for_timeout(50)
    return condition()


def hub_arm():
    return get_json('/api/arm')


def wait_text(page, element, text, timeout=5):
    """Waits until the element's text holds text, and says whether it did."""
    try:
        page.wait_for_function('([id, t]) => document.getElementById(id).textContent.includes(t)', arg=[element, text], timeout=timeout * 1000)
        return True
    except Exception:
        return False


def test_arming(page, dialogs):
    page.goto(URL + '#overview')
    check('the Overview page shows the hub armed away, as it starts', wait_text(page, 'armState', 'armed away'), page.text_content('#armState'))
    check('arming away again is not offered', page.is_disabled('#armAway'))

    page.click('#armHome')
    check('Arm home tells the page', wait_text(page, 'armState', 'armed home'), page.text_content('#armState'))
    check('and arms the hub home', hub_arm()['mode'] == 'home', hub_arm())
    page.click('#armAway')
    check('Arm away arms the hub away again', wait_text(page, 'armState', 'armed away') and hub_arm()['mode'] == 'away', hub_arm())

    # A disarm stops recording, so the page asks first; cancelled, nothing changes.
    dialogs.accept = False
    page.select_option('#armFor', '0')
    page.click('#disarmBtn')
    kind, message = dialogs.last()
    check('Disarm asks first, saying nothing will be recorded', kind == 'confirm' and 'nothing is recorded' in message, message)
    page.wait_for_timeout(500)
    check('a cancelled disarm leaves the hub armed', hub_arm()['mode'] == 'away' and 'armed away' in page.text_content('#armState'), hub_arm())

    dialogs.accept = True
    page.click('#disarmBtn')
    check('Disarm until armed again tells the page', wait_text(page, 'armState', 'disarmed until armed again'), page.text_content('#armState'))
    a = hub_arm()
    check('and disarms the hub with no end', a['mode'] == 'disarmed' and not a['timed'], a)
    check('the page warns that nothing is recorded', page.is_visible('#armWarn'))
    page.goto(URL + '#overview')  # the line at the top is read again on each page
    check('the line at the top says the hub is disarmed', wait_text(page, 'status', 'disarmed'), page.text_content('#status'))

    page.click('#armAway')
    check('arming hides the warning', wait_text(page, 'armState', 'armed away') and not page.is_visible('#armWarn'))
    page.select_option('#armFor', '30')
    page.click('#disarmBtn')
    check('Disarm for 30 minutes says when it arms again', wait_text(page, 'armState', 'arms again in 30 min'), page.text_content('#armState'))
    a = hub_arm()
    check('and the hub arms again in 30 minutes', a['mode'] == 'disarmed' and a['timed'] and 1700 <= (a['left_s'] or 0) <= 1800, a)


def test_a_refused_change_shows_the_truth(page, dialogs):
    # The hub refuses: the page must say so and go on showing what the hub really is, never what was asked for.
    def refuse(route):
        if route.request.method == 'POST':
            route.fulfill(status=507, content_type='application/json', body='{"ok":false,"error":"could not save the arm mode"}')
        else:
            route.continue_()

    page.route('**/api/arm', refuse)
    before = hub_arm()['mode']
    seen = len(dialogs.seen)
    page.click('#armHome')
    page_wait(page, lambda: len(dialogs.seen) > seen)
    kind, message = dialogs.last()
    check('a refused arm is shown with the hub\'s reason', kind == 'alert' and 'could not save' in message, message)
    page.wait_for_timeout(500)
    check('and the page still shows the hub as it is', before == 'disarmed' and hub_arm()['mode'] == 'disarmed'
          and 'disarmed' in page.text_content('#armState') and page.is_visible('#armWarn'), page.text_content('#armState'))
    page.unroute('**/api/arm')


def open_alert_settings(page):
    """Opens the sensor's Alert settings, where its test alert button is; the page closes them when it redraws."""
    page.wait_for_selector('summary:has-text("Alert settings")')
    if not page.is_visible('button:has-text("Send test alert")'):
        page.click('summary:has-text("Alert settings")')


def end_alert(nvr):
    control('end-alerts')
    wait_until(lambda: nvr.states()[-1:] == ['false'], timeout=5)


def test_alerts(page, dialogs, nvr):
    # Still disarmed from above: a test alert takes the whole path whatever the alarm is set to.
    check('the test alerts run while the hub is disarmed', hub_arm()['mode'] == 'disarmed')
    page.goto(URL + '#sensors')
    open_alert_settings(page)

    dialogs.accept = False
    sent = len(nvr.events)
    page.click('button:has-text("Send test alert")')
    kind, message = dialogs.last()
    check('the Sensors page asks before a test alert', kind == 'confirm' and 'test alert' in message, message)
    page.wait_for_timeout(500)
    check('a cancelled test alert sends nothing', 'true' not in nvr.states()[sent:], nvr.states()[sent:])

    dialogs.accept = True
    open_alert_settings(page)
    page.click('button:has-text("Send test alert")')
    check('the Sensors page says the test alert was sent', wait_text(page, 'msg', 'Sent a test alert'), page.text_content('#msg'))
    check('and the NVR gets it', wait_until(lambda: 'true' in nvr.states()[sent:], timeout=5), nvr.states()[sent:])
    newest = event_list()[0]
    check('logged as a test from that sensor', newest['sensor'] == SENSOR and newest['event'] == 'test alert', newest)
    end_alert(nvr)

    sent = len(nvr.events)
    page.goto(URL + '#sensor:' + SENSOR)
    page.wait_for_selector('#snTest')
    page.click('#snTest')
    check("the sensor's own page says the test alert was sent", wait_text(page, 'snMsg', 'Test alert sent'), page.text_content('#snMsg'))
    check('and the NVR gets that one too', wait_until(lambda: 'true' in nvr.states()[sent:], timeout=5), nvr.states()[sent:])
    end_alert(nvr)


def main():
    if DEVICE:
        sys.exit('the page test changes the arm mode, so it runs against the PC build only')
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage:
        proc = start_hub(storage, PASSWORD)
        try:
            check('server starts', common.HUB.stdout.readline().strip() == 'ready')
            check('a sensor to test with', api('POST', f'/api/mkdir?path={SENSOR}')[0] == 200)
            nvr = FakeNvrListener()
            inner = f'<ConsumerReference><Address>http://{LOCAL}:{nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
            check('a pretend NVR subscribes', soap('Subscribe', '/onvif/device_service', inner)[0] == 200)
            wait_until(lambda: nvr.states()[:1] == ['false'])
            with sync_playwright() as p:
                browser = p.chromium.launch()
                page = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD}).new_page()
                errors = []
                page.on('pageerror', lambda e: errors.append(str(e)))
                dialogs = Dialogs(page)
                for test in (lambda: test_arming(page, dialogs), lambda: test_a_refused_change_shows_the_truth(page, dialogs),
                             lambda: test_alerts(page, dialogs, nvr)):
                    try:
                        test()
                    except Exception as e:  # a control that never appeared: a failure, and the rest still run
                        check('the page could be used', False, str(e).splitlines()[0])
                check('no script errors on the pages', not errors, errors)
                browser.close()
        finally:
            proc.kill()
            proc.wait()
    print(f'\n{len(failures)} failure(s)' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
