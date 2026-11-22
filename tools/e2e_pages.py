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
    """The browser's own alert, confirm and prompt boxes, which the pages no longer use: each is noted, as a failure, and
    accepted so the test goes on."""

    def __init__(self, page):
        self.seen = []
        page.on('dialog', self.answer)

    def answer(self, d):
        self.seen.append((d.type, d.message))
        d.accept()


def answer(page, accept=True):
    """Answers the question the page asks in its own dialog, confirming or cancelling, and returns its words, title
    first; '' when no question came."""
    try:
        page.wait_for_selector('dialog#ask[open]', timeout=5000)
    except Exception:
        return ''
    words = page.text_content('dialog#ask')
    page.click('#askOk' if accept else '#askCancel')
    page.wait_for_selector('dialog#ask', state='detached', timeout=5000)
    return words


def notice(page, text, timeout=5):
    """Waits until the notice at the foot of the page holds text, and says whether it did."""
    try:
        page.wait_for_function('t => { const n = document.getElementById("toast"); return !n.hidden && n.textContent.includes(t); }',
                               arg=text, timeout=timeout * 1000)
        return True
    except Exception:
        return False


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


def panel(page):
    return page.text_content('#armPanel') + ' | ' + page.get_attribute('#armPanel', 'class')


def test_arming(page, dialogs):
    page.goto(URL + '#overview')
    check('the Overview page shows the hub armed away, as it starts', wait_text(page, 'armTitle', 'Armed away'), panel(page))
    check('in the colours for armed away, with the same at the top of the page',
          'away' in page.get_attribute('#armPanel', 'class') and wait_text(page, 'modeChip', 'Armed away'), panel(page))
    check('arming away again is not offered', page.is_disabled('#armAway'))

    page.click('#armHome')
    check('Arm home tells the page', wait_text(page, 'armTitle', 'Armed home') and 'home' in page.get_attribute('#armPanel', 'class'), panel(page))
    check('and arms the hub home', hub_arm()['mode'] == 'home', hub_arm())
    check('and says so at the foot of the page, with a way back', notice(page, 'Armed home') and page.is_visible('#toast button'),
          page.text_content('#toast'))
    page.click('#toast button')
    check('Undo arms the hub away again, as it was', wait_text(page, 'armTitle', 'Armed away') and hub_arm()['mode'] == 'away', hub_arm())
    page.click('#armHome')
    wait_text(page, 'armTitle', 'Armed home')
    page.click('#armAway')
    check('Arm away arms the hub away again', wait_text(page, 'armTitle', 'Armed away') and hub_arm()['mode'] == 'away', hub_arm())

    # A disarm stops recording, so the page asks first, in the page; cancelled, nothing changes.
    page.select_option('#armFor', '0')
    page.click('#disarmBtn')
    words = answer(page, accept=False)
    check('Disarm asks first, in the page, saying nothing will be recorded', 'Disarm the hub until armed again?' in words and 'nothing is recorded' in words, words)
    page.wait_for_timeout(500)
    check('a cancelled disarm leaves the hub armed', hub_arm()['mode'] == 'away' and 'Armed away' in page.text_content('#armTitle'), hub_arm())

    page.click('#disarmBtn')
    answer(page)
    check('Disarm until armed again tells the page', wait_text(page, 'armState', 'Until armed again') and wait_text(page, 'armTitle', 'Disarmed'), panel(page))
    a = hub_arm()
    check('and disarms the hub with no end', a['mode'] == 'disarmed' and not a['timed'], a)
    check('the page warns that nothing is recorded', page.is_visible('#armWarn'))
    check('the top of the page says disarmed', wait_text(page, 'modeChip', 'Disarmed'), page.text_content('#modeChip'))
    page.goto(URL + '#overview')  # the line at the top is read again on each page
    check('the line at the top says the hub is disarmed', wait_text(page, 'status', 'disarmed'), page.text_content('#status'))

    page.click('#armAway')
    check('arming hides the warning', wait_text(page, 'armTitle', 'Armed away') and not page.is_visible('#armWarn'))
    page.select_option('#armFor', '30')
    page.click('#disarmBtn')
    answer(page)
    check('Disarm for 30 minutes says when it arms again', wait_text(page, 'armState', 'Arms again in 30 min'), panel(page))
    a = hub_arm()
    check('and the hub arms again in 30 minutes', a['mode'] == 'disarmed' and a['timed'] and 1700 <= (a['left_s'] or 0) <= 1800, a)


def test_a_refused_change_shows_the_truth(page, dialogs):
    # The hub refuses: the page must say so and go on showing what the hub really is, never what was asked for.
    def refuse(route):
        if route.request.method == 'POST':
            route.fulfill(status=507, content_type='application/json', body='{"ok":false,"error":"could not save the arm mode"}')
        else:
            route.continue_()

    api('POST', '/api/arm', 'mode=disarmed&for=0')
    page.goto(URL + '#overview')
    wait_text(page, 'armTitle', 'Disarmed')
    page.route('**/api/arm', refuse)
    before = hub_arm()['mode']
    page.click('#armHome')
    check('a refused arm is shown with the hub\'s reason', notice(page, 'could not save') and 'err' in page.get_attribute('#toast', 'class'),
          page.text_content('#toast'))
    page.wait_for_timeout(500)
    check('and the page still shows the hub as it is', before == 'disarmed' and hub_arm()['mode'] == 'disarmed'
          and 'Disarmed' in page.text_content('#armTitle') and page.is_visible('#armWarn'), panel(page))
    page.unroute('**/api/arm')


def test_a_refused_alarm_switch_goes_back(page, dialogs):
    # The switch that makes node problems raise the alarm: refused, it must show what the hub has, and say why.
    def refuse(route):
        if route.request.method == 'POST':
            route.fulfill(status=507, content_type='application/json', body='{"ok":false,"error":"could not save the setting"}')
        else:
            route.continue_()

    api('POST', '/api/alerts', 'alarm=0')
    page.goto(URL + '#espnow')
    page.wait_for_selector('#espAlarm', state='visible')
    page.route('**/api/alerts', refuse)
    page.click('#espAlarm')
    check('a refused alarm switch says why', notice(page, 'could not save') and 'err' in page.get_attribute('#toast', 'class'), page.text_content('#toast'))
    check('and goes back to what the hub has', not page.is_checked('#espAlarm') and get_json('/api/alerts')['alarm'] is False)
    page.unroute('**/api/alerts')
    page.click('#espAlarm')
    check('accepted, it says so', notice(page, 'now raise the alarm') and page.is_checked('#espAlarm') and get_json('/api/alerts')['alarm'] is True)
    api('POST', '/api/alerts', 'alarm=0')


def test_a_sealed_backup_from_the_page(page):
    # The passphrase at the top of Backup and restore seals the backup, with a key this page works out by PBKDF2; the
    # hub never spends the time. Downloading asks for the passphrase twice.
    page.goto(URL + '#maintenance')
    page.wait_for_selector('#bkPass', state='visible')
    derive = 'async ([p, s, n]) => Array.from(await window.backupKey(p, new TextEncoder().encode(s), n), (v) => v.toString(16).padStart(2, "0")).join("")'
    got = page.evaluate(derive, ['password', 'salt', 4096])
    check("the page's PBKDF2-HMAC-SHA256 gives the known answer", got == 'c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a', got)
    long_pass = 'x' * 70  # longer than a block, so hashed first
    started = time.time()
    got = page.evaluate(derive, [long_pass, 'salty salt', 600000])
    took = time.time() - started
    want = hashlib.pbkdf2_hmac('sha256', long_pass.encode(), b'salty salt', 600000).hex()
    check(f'and the same as Python at the full 600000 rounds, in {took:.1f} s', got == want, (got, want))
    page.fill('#bkPass', 'correct horse battery')
    page.fill('#bkPass2', 'correct horse batterz')
    page.click('#bkSave')
    check('two passphrases that differ are refused on the page', wait_text(page, 'rsMsg', 'differ'), page.text_content('#rsMsg'))
    page.fill('#bkPass2', 'correct horse battery')
    with page.expect_download(timeout=60000) as got:
        page.click('#bkSave')
    path = got.value.path()
    data = open(path, 'rb').read()
    check('the backup downloads sealed', data.startswith(b'sensorcam-backup sealed 2 pbkdf2-sha256 600000 '), data[:60])
    page.set_input_files('#rsFile', path)
    page.fill('#bkPass', 'wrong passphrase')
    page.click('#rsSend')
    answer(page)
    check('restoring it with a wrong passphrase says so', wait_text(page, 'rsMsg', 'passphrase is wrong', timeout=60), page.text_content('#rsMsg'))
    page.fill('#bkPass', 'correct horse battery')
    page.click('#rsSend')
    answer(page)
    check('with the right one, it restores', wait_text(page, 'rsMsg', 'Restored', timeout=60), page.text_content('#rsMsg'))


def open_alert_settings(page):
    """Opens the sensor's Alert settings, where its test alert button is; the page closes them when it redraws."""
    page.wait_for_selector('summary:has-text("Alert settings")')
    if not page.is_visible('button:has-text("Send test alert")'):
        page.click('summary:has-text("Alert settings")')


def end_alert(nvr):
    control('end-alerts')
    wait_until(lambda: nvr.states()[-1:] == ['false'], timeout=5)


def test_alerts(page, dialogs, nvr):
    # Disarmed: a test alert takes the whole path whatever the alarm is set to.
    api('POST', '/api/arm', 'mode=disarmed&for=0')
    check('the test alerts run while the hub is disarmed', hub_arm()['mode'] == 'disarmed')
    page.goto(URL + '#sensors')
    open_alert_settings(page)

    sent = len(nvr.events)
    page.click('button:has-text("Send test alert")')
    words = answer(page, accept=False)
    check('the Sensors page asks before a test alert', 'test alert' in words, words)
    page.wait_for_timeout(500)
    check('a cancelled test alert sends nothing', 'true' not in nvr.states()[sent:], nvr.states()[sent:])

    open_alert_settings(page)
    page.click('button:has-text("Send test alert")')
    answer(page)
    check('the Sensors page says the test alert was sent', notice(page, 'Sent a test alert'), page.text_content('#toast'))
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
    page.goto(URL + '#overview')
    page.evaluate('localStorage.clear()')
    page.reload()
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


def test_settings_pages(page):
    """A settings page holds its changes until they are saved or discarded, and asks before they are left behind; a file
    is chosen in a drop zone; the events have coloured labels."""
    page.goto(URL + '#ha')
    page.wait_for_selector('#mqUser', state='visible')
    page.wait_for_timeout(300)
    check('a page with nothing changed has no unsaved bar', not page.is_visible('#unsaved'))
    page.fill('#mqUser', 'someone')
    check('a change shows the unsaved bar', page_wait(page, lambda: page.is_visible('#unsaved')))
    page.evaluate("location.hash = 'overview'")
    words = answer(page, False)
    check('leaving asks first, and Stay keeps the page and the change',
          'not saved' in words and page.is_visible('#view-ha') and page.input_value('#mqUser') == 'someone', words)
    page.click('#unsavedDiscard')
    check('Discard puts back what the hub has', page_wait(page, lambda: page.input_value('#mqUser') == '' and not page.is_visible('#unsaved')),
          page.input_value('#mqUser'))
    page.fill('#mqUser', 'someone')
    page.evaluate("location.hash = 'overview'")
    answer(page, True)
    check('Leave goes on, and forgets the change', page_wait(page, lambda: page.is_visible('#view-overview') and not page.is_visible('#unsaved')))

    page.goto(URL + '#maintenance')
    page.wait_for_selector('#rbTime', state='visible')
    page.wait_for_timeout(300)
    page.fill('#rbTime', '03:15')
    page.click('#unsavedSave')
    check("the bar's Save saves that part", page_wait(page, lambda: get_json('/api/schedule')['minute'] == 3 * 60 + 15), get_json('/api/schedule'))
    check('and the bar goes', page_wait(page, lambda: not page.is_visible('#unsaved')))
    page.set_input_files('#fwFile', files=[{'name': 'firmware.bin', 'mimeType': 'application/octet-stream', 'buffer': b'\xe9' + b'\x01' * 30000}])
    check('a chosen file shows with its size', 'firmware.bin, 29 KB' in page.text_content('#view-maintenance .drop'),
          page.text_content('#view-maintenance .drop'))
    page.click('#fwSend')
    answer(page, True)
    check('the page says why the hub refused unsigned firmware', wait_text(page, 'fwMsg', 'not signed', 20), page.text_content('#fwMsg'))
    check('and hides the progress again', not page.is_visible('#fwProg'))

    # A node that has not said which hardware it runs cannot take settings from the hub, and the page says why.
    page.goto(URL + '#sensors')
    page.wait_for_selector('#tree summary:has-text("Hardware")')
    page.click('#tree summary:has-text("Hardware")')
    check("a node's Hardware says when its firmware is too old to take settings",
          'from before hardware settings' in page.text_content('#tree details:has(summary:has-text("Hardware"))')
          and page.is_disabled('#tree button:has-text("Save hardware")'))

    # A node's own page, from its name on the Sensors page.
    page.click('#tree .nodeName a')
    page.wait_for_selector('#ndFacts tr')
    node = SENSOR.split('/')[0]
    check("a node's name on the Sensors page opens its own page", page.evaluate('location.hash') == '#node:' + node
          and node in page.text_content('#ndFacts'), page.evaluate('location.hash'))
    check('which shows each of its sensors', page.locator('#ndSensors section').count() >= 1)

    # "Add a node" on the Nodes and pairing page, with the first steps for a new node.
    page.goto(URL + '#espnow')
    page.wait_for_selector('#guideSteps li')
    check('"Add a node" shows its steps', 'Let it pair' in page.text_content('#guideSteps'), page.text_content('#guideSteps'))

    get(f'/sensor?{sid(1)}')  # an alert of its own to see there, whatever ran before
    control('end-alerts')
    page.goto(URL + '#events')
    page.wait_for_selector('#evTable .chip')
    check('alerts are labelled in red', page.locator('#evTable .chip.bad').count() > 0)
    check('times are words, with the date on hover', page.get_attribute('#evTable tr:nth-child(2) td:first-child', 'title') not in (None, ''))


def test_quiet_when_hidden(browser):
    """A page left open in a tab that is not shown asks the hub nothing, and asks again once it is shown; and the
    Overview asks for no more than its summary between changes."""
    page = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD}).new_page()
    try:
        asked = []
        page.on('request', lambda r: asked.append(r.url))
        page.goto(URL + '#overview')
        page.wait_for_selector('#armPanel')
        page.wait_for_timeout(1500)
        asked.clear()
        page.wait_for_timeout(7000)
        check('between changes the Overview asks only for its summary', asked and all('/api/overview' in u for u in asked), asked)
        page.evaluate("Object.defineProperty(document, 'hidden', {get: () => true, configurable: true});"
                      "document.dispatchEvent(new Event('visibilitychange'))")
        asked.clear()
        page.wait_for_timeout(7000)
        check('a hidden tab asks the hub nothing', not asked, asked)
        page.evaluate("Object.defineProperty(document, 'hidden', {get: () => false, configurable: true});"
                      "document.dispatchEvent(new Event('visibilitychange'))")
        page.wait_for_timeout(500)
        check('shown again, it asks at once', any('/api/overview' in u for u in asked), asked)
    finally:
        page.context.close()


AXE_URL ='https://cdnjs.cloudflare.com/ajax/libs/axe-core/4.10.2/axe.min.js'
AXE_SHA256 = 'b511cd9dec01c76f4b2ad1723b66b6db37d4c2eb4ed199076e1829d9ee7b75e3'


def axe_source():
    """axe-core, the accessibility checker, downloaded once into .pio and checked against its known hash."""
    import urllib.request
    path = os.path.join(ROOT, '.pio', 'axe-core-4.10.2.min.js')
    if not os.path.exists(path):
        data = urllib.request.urlopen(AXE_URL, timeout=60).read()
        if hashlib.sha256(data).hexdigest() != AXE_SHA256:
            raise RuntimeError('axe-core did not match its hash')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
    with open(path, encoding='utf-8') as f:
        return f.read()


def test_accessibility(browser, dark=False):
    """Every page, light and dark, without what axe-core counts as a serious or critical fault: missing labels and
    names, too little contrast, broken landmarks, controls that cannot be reached."""
    source = axe_source()
    page = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD},
                               color_scheme='dark' if dark else 'light').new_page()
    try:
        page.goto(URL + '#overview')
        page.wait_for_selector('#view-overview', state='visible')
        page.add_script_tag(content=source)
        views = page.eval_on_selector_all('.item', 'items => items.map(i => i.dataset.view)')
        for view in views:
            page.evaluate(f"location.hash = '{view}'")
            page.wait_for_timeout(900)  # the page fills its tables from the hub
            faults = page.evaluate("""async () => {
                const r = await axe.run(document, { iframes: false, resultTypes: ['violations'] });
                return r.violations.filter(v => v.impact == 'serious' || v.impact == 'critical')
                    .map(v => v.id + ': ' + v.nodes.slice(0, 3).map(n => n.target.join(' ')).join(', '));
            }""")
            check(f"{view}{' (dark)' if dark else ''}: no serious accessibility faults", not faults, faults)
    finally:
        page.context.close()


def test_a_sensor_camera(page):
    page.goto(URL + '#sensors')
    open_alert_settings(page)
    box = page.locator('input[placeholder^="such as Camera"]').first
    box.fill('Camera 2')
    page.locator('button:has-text("Save alert settings")').first.click()
    entry = poll(lambda: get_json('/api/tree'), lambda t: any(n.get('camera') == 'Camera 2' for s in t['nodes'] for n in s['sensors']))
    check('a camera saved on the Sensors page is kept', any(n.get('camera') == 'Camera 2' for s in entry['nodes'] for n in s['sensors']))
    api('POST', f'/api/sensor-settings?path={SENSOR}', '')


def test_sensor_pictures(page):
    # Each picture by what it is shown for, not by its file name, and a question in the page before anything is deleted.
    page.goto(URL + '#sensors')
    page.wait_for_selector('.slots')
    grid = page.text_content('.slots')
    check('the pictures are named by what they show', all(w in grid for w in ('Seen', 'Coming closer', 'Going away', 'Left to right',
                                                                             'Right to left', 'Shown for a past event')), grid)
    check('the file names are hidden', not page.is_visible('.slots .fname'))
    page.check('#showNames')
    check('until asked for', page.is_visible('.slots .fname') and 'approaching-delayed.h264' in page.text_content('.slots'))
    page.uncheck('#showNames')
    check('a sensor with no pictures shows the built-in ones', 'Built-in' in grid and 'Same as Seen' not in grid, grid)
    page.click('.slots .slot button')
    words = answer(page, accept=False)
    check("a picture's Change button offers what can be done to it", 'Live alert: seen' in words and 'Upload a .h264 file' in words, words)
    page.click('.sensor button.del')
    try:
        page.wait_for_selector('dialog#ask[open]', timeout=5000)
        red = page.get_attribute('#askOk', 'class')
    except Exception:
        red = ''
    words = answer(page, accept=False)
    check('deleting a sensor asks first, with a red button', 'Delete the sensor' in words and 'danger' in red, (words, red))
    page.wait_for_timeout(300)
    check('and a cancelled delete keeps it', any(n['input'] == 1 for s in get_json('/api/tree')['nodes'] for n in s['sensors'] if s['mac'] == A))


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


def own_hub(test):
    """The test, on a hub of its own with fresh storage and no password, as a hub flashed from a release starts; the
    hub the other tests use is started again afterwards, as it was."""
    def run(browser, storage):
        common.HUB.kill()
        common.HUB.wait()
        try:
            with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as own:
                start_hub(own, '')  # said as empty: with none said, the PC build takes the one compiled in
                try:
                    test(browser)
                finally:
                    common.HUB.kill()
                    common.HUB.wait()
        finally:
            start_hub(storage, PASSWORD)
            common.HUB.stdout.readline()
    return run


@own_hub
def test_a_hub_without_a_password_asks_for_one(browser):
    # A hub can start with no password, from secrets.h or an older firmware that let it be turned off. Every page then
    # asks for one, and the page will not take two that differ or one that is too short.
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


@own_hub
def test_a_blank_hub_is_set_up(browser):
    # A hub flashed from a release has no Wi-Fi and no password, and its setup network is open. Its setup page asks for
    # the hub password with the network, so the hub is never left without one, and the API refuses to go on without it.
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


class Tab:
    """A fresh browser page for one test, logged in, noting its script errors and any browser dialog box."""

    def __init__(self, browser):
        self.context = browser.new_context(http_credentials={'username': 'admin', 'password': PASSWORD})
        self.page = self.context.new_page()
        self.errors = []
        self.page.on('pageerror', lambda e: self.errors.append(str(e)))
        self.dialogs = Dialogs(self.page)

    def close(self):
        check('no script errors on the pages', not self.errors, self.errors)
        check("and none of the browser's own dialog boxes: the pages ask in the page", not self.dialogs.seen, self.dialogs.seen)
        self.context.close()


def page_cases(nvr, storage):
    """Each test by name, given a fresh page, or the browser for those that open their own."""
    def on_page(test, *more):
        def run(browser):
            tab = Tab(browser)
            try:
                test(tab.page, *[tab.dialogs if m == 'dialogs' else m for m in more])
            finally:
                tab.close()
        return run
    return [
        ('arming', on_page(test_arming, 'dialogs')),
        ('a_refused_change_shows_the_truth', on_page(test_a_refused_change_shows_the_truth, 'dialogs')),
        ('alerts', on_page(test_alerts, 'dialogs', nvr)),
        ('a_refused_alarm_switch_goes_back', on_page(test_a_refused_alarm_switch_goes_back, 'dialogs')),
        ('the_first_steps', on_page(test_the_first_steps)),
        ('notifications_page', on_page(test_notifications_page)),
        ('settings_pages', on_page(test_settings_pages)),
        ('a_sealed_backup_from_the_page', on_page(test_a_sealed_backup_from_the_page)),
        ('a_sensor_camera', on_page(test_a_sensor_camera)),
        ('sensor_pictures', on_page(test_sensor_pictures)),
        ('the_wifi_page_frames_the_setup_page', on_page(test_the_wifi_page_frames_the_setup_page)),
        ('every_page_opens', test_every_page_opens),
        ('dark_and_narrow', test_dark_and_narrow),
        ('quiet_when_hidden', test_quiet_when_hidden),
        ('accessibility', test_accessibility),
        ('accessibility_dark', lambda browser: test_accessibility(browser, dark=True)),
        ('a_hub_without_a_password_asks_for_one', lambda browser: test_a_hub_without_a_password_asks_for_one(browser, storage)),
        ('a_blank_hub_is_set_up', lambda browser: test_a_blank_hub_is_set_up(browser, storage)),
    ]


def main():
    if DEVICE:
        sys.exit('the page test changes the arm mode, so it runs against the PC build only')
    import argparse
    parser = argparse.ArgumentParser(description='The web pages in a real browser, against the PC build.')
    parser.add_argument('words', nargs='*', help='run only the tests named, or with any of these in their names')
    parser.add_argument('--list', action='store_true', help='list the tests and stop')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as storage:
        cases = page_cases(None, storage)
        names = [n for n, _ in cases]
        if args.list:
            print('\n'.join(names))
            return
        chosen = [n for n in names if not args.words or any(w == n or (w not in names and w in n) for w in args.words)]
        if not chosen:
            sys.exit('no test has any of these in its name: ' + ' '.join(args.words))
        start_hub(storage, PASSWORD)
        failed = []
        try:
            check('server starts', common.HUB.stdout.readline().strip() == 'ready')
            check('a sensor to test with', api('POST', f'/api/mkdir?path={SENSOR}')[0] == 200)
            nvr = FakeNvrListener()
            inner = f'<ConsumerReference><Address>http://{LOCAL}:{nvr.port}/onvif_notify_server/</Address></ConsumerReference>'
            check('a pretend NVR subscribes', soap('Subscribe', '/onvif/device_service', inner)[0] == 200)
            wait_until(lambda: nvr.states()[:1] == ['false'])
            cases = dict(page_cases(nvr, storage))
            with sync_playwright() as p:
                browser = p.chromium.launch()
                for n in chosen:
                    print(f'\n-- {n}', flush=True)
                    before = len(failures)
                    control('end-alerts')  # each starts from a quiet hub, armed away
                    if hub_arm()['mode'] != 'away':
                        api('POST', '/api/arm', 'mode=away')
                    try:
                        cases[n](browser)
                    except Exception as e:  # a control that never appeared: a failure, and the rest still run
                        check(f'{n}: the page could be used', False, str(e).splitlines()[0])
                    if len(failures) > before:
                        failed.append(n)
                browser.close()
        finally:
            common.HUB.kill()
            common.HUB.wait()
    if failed:
        print(f'\n{len(failures)} failure(s), in: ' + ' '.join(failed))
        print('to run just those again: py tools/e2e_pages.py ' + ' '.join(failed))
    else:
        print('\nall passed' if not failures else f'\n{len(failures)} failure(s)')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
