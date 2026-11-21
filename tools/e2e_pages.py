"""The web pages in a real browser, against the PC build (pio run -e host): the controls that decide whether an intruder
is recorded. Arming and disarming on the Overview page, the test alert on the Sensors page and a sensor's own page, and a hub
without a password asking for one,
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


def test_the_first_steps(page):
    # The hub here has no notifications set up, so the Overview shows the next step, with what to do and a way there.
    page.evaluate('localStorage.clear()')
    page.goto(URL + '#overview')
    page.wait_for_selector('#wizard', state='visible')
    check('the Overview shows the next first step', 'Getting started' in page.text_content('#wizTitle') and page.text_content('#wizHelp'),
          page.text_content('#wizard'))
    step = page.text_content('#wizStep')
    page.click('#wizGo')
    page.wait_for_timeout(300)
    check('its button leads to the page for it', page.evaluate('location.hash') not in ('', '#overview') or 'Test' in step, (step, page.url))
    page.goto(URL + '#overview')
    page.wait_for_selector('#wizard', state='visible')
    page.click('#wizLater')
    check('it can be hidden for now', not page.is_visible('#wizard'))
    page.reload()
    page.wait_for_selector('#ovReadyBox', state='visible')
    check('and stays hidden, with the list at the foot of the page', not page.is_visible('#wizard'))
    page.click('#wizBack')
    page.wait_for_selector('#wizard', state='visible')
    check('and comes back when asked', page.is_visible('#wizard'))


def test_notifications_page(page):
    page.goto(URL + '#notify')
    page.wait_for_selector('#hookFormat', state='visible')
    page.select_option('#hookFormat', 'ntfy')
    check('ntfy asks for a topic and an access token', 'topic' in page.text_content('#hookHelp') and page.is_visible('#hookToken'),
          page.text_content('#hookHelp'))
    page.select_option('#hookFormat', 'json')
    check('JSON needs no token', not page.is_visible('#hookToken'))
    page.select_option('#hookFormat', 'gotify')
    page.fill('#hookUrl', f'http://127.0.0.1:9/message')
    page.fill('#hookToken', '')
    page.click('#saveHook')
    check('Gotify without its token is refused, with the reason', wait_text(page, 'hookMsg', 'token'), page.text_content('#hookMsg'))
    page.select_option('#hookFormat', 'ntfy')
    page.fill('#hookUrl', 'http://127.0.0.1:9/alarm')
    page.fill('#hookToken', 'tk_page1')
    page.select_option('#hookWeekDay', '0')
    page.fill('#hookWeekTime', '18:30')
    page.click('#saveHook')
    check('ntfy with a token and a weekly summary is saved', wait_text(page, 'hookMsg', 'Saved'), page.text_content('#hookMsg'))
    saved = get_json('/api/webhook')
    check('as the page said', saved['format'] == 'ntfy' and saved['token'] is True and saved['weekday'] == 0 and saved['weekminute'] == 18 * 60 + 30,
          saved)
    page_wait(page, lambda: 'leave blank' in (page.get_attribute('#hookToken', 'placeholder') or ''))
    check('the page says a token is saved without showing it', page.input_value('#hookToken') == ''
          and 'leave blank' in page.get_attribute('#hookToken', 'placeholder'))
    api('POST', '/api/webhook', 'url=&alerts=1&problems=1&forget=1')


def test_a_sensor_camera(page):
    page.goto(URL + '#sensors')
    open_alert_settings(page)
    box = page.locator('input[placeholder^="such as Camera"]').first
    box.fill('Camera 2')
    page.locator('button:has-text("Save alert settings")').first.click()
    entry = poll(lambda: get_json('/api/tree'), lambda t: any(n.get('camera') == 'Camera 2' for s in t['nodes'] for n in s['sensors']))
    check('a camera saved on the Sensors page is kept', any(n.get('camera') == 'Camera 2' for s in entry['nodes'] for n in s['sensors']))
    api('POST', f'/api/sensor-settings?path={SENSOR}', '')


def test_every_page_opens(browser):
    # Each page in the menu, opened in turn, with no script error and something shown: each has its own script, and a
    # page none of the other tests opens would otherwise fail unseen.
    page = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD}).new_page()
    errors = []
    page.on('pageerror', lambda e: errors.append(str(e)))
    page.goto(URL + '#overview')
    views = page.eval_on_selector_all('.item', 'items => items.map(i => i.dataset.view)')
    empty = []
    for view in views:
        page.goto(URL + '#' + view)
        page.wait_for_timeout(700)  # each loads what it shows from the hub
        shown = page.eval_on_selector(f'#view-{view}', 'v => !v.hidden && v.innerText.trim().length')
        if not shown:
            empty.append(view)
    check(f'all {len(views)} pages open, each showing something', len(views) >= 18 and not empty, (views, empty))
    check('with no script error on any', not errors, errors)
    page.close()


def test_the_wifi_page_frames_the_setup_page(page):
    # The hub's own frame still works under the rule that no other site may frame its pages.
    page.goto(URL + '#wifi')
    try:
        page.frame_locator('#netFrame').locator('h1').wait_for(timeout=5000)
        title = page.frame_locator('#netFrame').locator('h1').text_content()
    except Exception as e:
        title = str(e).splitlines()[0]
    check('the Wi-Fi page shows the setup page in its frame', title == 'Hub Wi-Fi setup', title)


def test_dark_and_narrow(browser):
    page = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD}, color_scheme='dark',
                               viewport={'width': 390, 'height': 800}).new_page()
    page.goto(URL + '#espnow')
    page.wait_for_selector('#view-espnow table', state='visible')
    page.wait_for_timeout(500)
    background = page.evaluate('getComputedStyle(document.body).backgroundColor')
    check('a device set to dark gets the dark pages', background == 'rgb(22, 25, 29)', background)
    wide = page.evaluate('document.documentElement.scrollWidth')
    check('on a phone the page is no wider than the screen, its tables scrolling inside it', wide <= 390, wide)
    page.close()


def test_a_hub_without_a_password_asks_for_one(browser, storage):
    # A hub can start with no password, from secrets.h or an older firmware that let it be turned off. Every page then
    # asks for one, and the page will not take two that differ or one that is too short.
    common.HUB.kill()
    common.HUB.wait()
    start_hub(storage)
    check('a hub without a password starts', common.HUB.stdout.readline().strip() == 'ready')
    page = browser.new_page()
    dialogs = Dialogs(page)
    page.goto(URL + '#overview')
    page.wait_for_selector('#needPw', state='visible')
    check('every page asks for a password', page.is_visible('#openWarn'))
    page.fill('#needPw', 'a-good-password')
    page.fill('#needPw2', 'another-password')
    page.click('#saveNeedPw')
    check('two passwords that differ are refused on the page', wait_text(page, 'needPwMsg', 'differ'), page.text_content('#needPwMsg'))
    page.fill('#needPw', 'short')
    page.fill('#needPw2', 'short')
    page.click('#saveNeedPw')
    check('a short one is refused, with the reason', wait_text(page, 'needPwMsg', '8 to 63 characters'), page.text_content('#needPwMsg'))
    page.goto(URL + '#http')
    check('and the HTTP page has no way to turn the password off', page.query_selector('#protect') is None)
    page.goto(URL + '#overview')
    page.wait_for_selector('#needPw', state='visible')
    page.fill('#needPw', 'a-good-password')
    page.fill('#needPw2', 'a-good-password')
    page.click('#saveNeedPw')
    check('a good one is saved', wait_text(page, 'needPwMsg', 'Saved'), page.text_content('#needPwMsg'))
    check('and guards the hub at once, without a restart', api('GET', '/api/tree', auth=False)[0] == 401)
    check('no dialogs on the way', not dialogs.seen, dialogs.seen)
    page.close()


def test_a_blank_hub_is_set_up(browser, storage):
    # A hub flashed from a release has no Wi-Fi and no password, and its setup network is open. Its setup page asks for
    # the hub password with the network, so the hub is never left without one, and the API refuses to go on without it.
    common.HUB.kill()  # the test before gave this hub a password, which now applies at once
    common.HUB.wait()
    start_hub(storage)
    check('a blank hub starts', common.HUB.stdout.readline().strip() == 'ready')
    control('setup-on')
    try:
        page = browser.new_page()
        dialogs = Dialogs(page)
        page.goto(URL + 'setup')
        page.wait_for_selector('#hubpw', state='visible')
        check('the setup page of a blank hub asks for a hub password', page.is_visible('#hubpw2'))
        page.select_option('#list', 'test-net')
        page.fill('#pass', 'p%ss w+rd&ok')
        page.fill('#hubpw', 'short')
        page.fill('#hubpw2', 'short')
        page.click('#save')
        check('a short hub password is refused on the page', wait_text(page, 'msg', '8 to 63 characters'), page.text_content('#msg'))
        page.fill('#hubpw', 'first-password')
        page.fill('#hubpw2', 'first-passwordd')
        page.click('#save')
        check('two hub passwords that differ are refused', wait_text(page, 'msg', 'differ'), page.text_content('#msg'))
        code = api('POST', '/api/wifi', 'ssid=test-net&password=p%25ss%20w%2Brd%26ok', auth=False)[0]
        check('the hub will not take a network without a hub password', code == 400, code)
        page.fill('#hubpw2', 'first-password')
        page.click('#save')
        check('the network and the hub password are saved together', wait_text(page, 'msg', 'with the hub password as its password'),
              page.text_content('#msg'))
        code = api('GET', '/api/tree', auth=False)[0]
        check('the new password guards the hub at once, before any restart', code == 401, code)
        c = http.client.HTTPConnection(HOST, WEB, timeout=5)
        c.request('GET', '/api/tree', headers={'Authorization': 'Basic ' + base64.b64encode(b'admin:first-password').decode()})
        r = c.getresponse()
        r.read()
        c.close()
        check('and opens it', r.status == 200, r.status)
        check('no dialogs on the way', not dialogs.seen, dialogs.seen)
        page.close()
    finally:
        control('setup-off')


def main():
    if DEVICE:
        sys.exit('the page test changes the arm mode, so it runs against the PC build only')
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage:
        start_hub(storage, PASSWORD)
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
                             lambda: test_alerts(page, dialogs, nvr), lambda: test_the_first_steps(page), lambda: test_notifications_page(page),
                             lambda: test_a_sensor_camera(page), lambda: test_the_wifi_page_frames_the_setup_page(page), lambda: test_every_page_opens(browser),
                             lambda: test_dark_and_narrow(browser)):
                    try:
                        test()
                    except Exception as e:  # a control that never appeared: a failure, and the rest still run
                        check('the page could be used', False, str(e).splitlines()[0])
                check('no script errors on the pages', not errors, errors)
                try:
                    test_a_hub_without_a_password_asks_for_one(browser, storage)
                except Exception as e:
                    check('the password page could be used', False, str(e).splitlines()[0])
                try:
                    test_a_blank_hub_is_set_up(browser, storage)
                except Exception as e:
                    check('the setup page could be used', False, str(e).splitlines()[0])
                browser.close()
        finally:
            common.HUB.kill()
            common.HUB.wait()
    print(f'\n{len(failures)} failure(s)' if failures else '\nall passed')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
