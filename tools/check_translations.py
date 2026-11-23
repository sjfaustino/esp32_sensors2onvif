"""Checks that every phrase of the pages, and of the hub's own messages, has its translation in each language offered in
i18n/ (tools/i18n.py), so a page or a notification never shows English in the middle of another language. Run with the
other quick checks (tools/precheck.py), by CI and the pre-push hook. It fails with each phrase missing or fuzzy, each
count without its plural forms, each phrase no longer used, each translation whose {names} differ from its English, and
each piece of English in a script that does not go through tr(). A language not yet marked X-Complete: yes is only
counted, not failed.

The pages' phrases are: the text and the placeholder, title, alt and aria-label words of web/index.html and
web/setup.html; the phrase of each tr(), trc() and trParts() in the scripts, and the pair of each trCount(); and the
messages of the hub the pages show, each jsonFail() and saved() with its message in one string, each event's name, and
the other words listed in HUB_WORDS. A phrase of the hub's that changes, such as one with a count in it, is in the .po
file with {name} for that part. The hub's own messages, with the context "message", are the phrases of camText() and
camCount() in its C++ and the words of MESSAGE_WORDS, which it looks up as they come.

A string in a script that reads like English, a capital and a space, or three words, or words ending a sentence, must be
inside tr() or trCount(), or on a line marked // not words, as for a code or a name.
"""
import json
import os
import re
import sys
from html.parser import HTMLParser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
WEB = os.path.join(ROOT, 'web')
LIB = os.path.join(ROOT, 'lib', 'sensorcam')
STRING = r'"((?:[^"\\\n]|\\.)*)"'
# Names, codes and words that read the same in both languages, as they appear in the HTML.
SAME = {'ONVIF', 'RTSP', 'HTTP', 'NVR', 'MQTT', 'Wi-Fi', 'Home Assistant', 'ESP-NOW', 'HTTPS', 'PIN', 'ID', 'OK', 'IP',
        'SSID', 'MAC', 'CSV', 'JSON', 'H.264', 'JPEG', 'dBm', 'KB', 'MB', 'GPIO', 'UTC', 'NTP', 'DHCP', 'DNS', 'URL', 'Português',
        'English', 'ntfy', 'Gotify'}
# A file name, an address or a code, as in <code>, which reads the same in any language.
CODE = re.compile(r'^[\w.:-]*[.:-][\w.:-]*$')


def words(text):
    return ' '.join(text.split())


def unquote(s):
    return json.loads('"' + s + '"')


def has_letters(text):
    return re.search(r'[A-Za-z]{2}', text) is not None


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.found = set()
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in ('script', 'style'):
            self.skip += 1
        for k, v in attrs:
            if k in ('placeholder', 'title', 'alt', 'aria-label') and v and has_letters(v):
                self.found.add(words(v))

    def handle_endtag(self, tag):
        if tag in ('script', 'style'):
            self.skip -= 1

    def handle_data(self, data):
        if not self.skip and has_letters(data):
            self.found.add(words(data))


def page_phrases():
    found = set()
    for name in ('index.html', 'setup.html'):
        p = Page()
        p.feed(open(os.path.join(WEB, name), encoding='utf-8').read())
        found |= p.found
        title = re.search(r'<title>(.*?)</title>', open(os.path.join(WEB, name), encoding='utf-8').read())
        if title:
            found.add(title.group(1))
    return {f for f in found if f not in SAME and not CODE.match(f)}


# Short English words that turn up in a phrase, and not in a class name, a selector or a code.
COMMON = {'a', 'an', 'the', 'is', 'are', 'was', 'be', 'been', 'not', 'no', 'to', 'of', 'and', 'or', 'for', 'with', 'set',
          'up', 'yet', 'on', 'off', 'at', 'in', 'it', 'its', 'this', 'that', 'by', 'from', 'has', 'have', 'will', 'can',
          'cannot', 'must', 'still', 'again', 'none', 'all', 'every', 'each', 'your', 'you', 'here', 'now', 'saved',
          'answer', 'sent', 'heard', 'left', 'more', 'less', 'first', 'last', 'never', 'only'}


def looks_like_words(s):
    """Whether a script's string reads like English for the page: a capital and a space, three words, two ending a
    sentence, or two with a common English word among them."""
    if not re.search(r'[a-z]{2}', s) or re.search(r'^[#./]|[{}<>=]|^\w+:\/\/', s):
        return False
    words_in = s.split()
    n = len(words_in)
    common = any(w.lower().strip('.,:;?!()') in COMMON for w in words_in)
    return bool(re.match(r'^[A-Z][a-z]*\s', s) or n >= 3 or (n >= 2 and (re.search(r'[.:?!]\s*$', s) or common)))


def double_quoted(text):
    """The script with each 'string' written as a "string", as Prettier writes one that holds a double quote, so one
    pattern reads them all. Comments and template strings are kept as they are."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i) or text.startswith('/*', i):
            end = text.find('\n' if text[i + 1] == '/' else '*/', i)
            end = n if end < 0 else end + (0 if text[i + 1] == '/' else 2)
            out.append(text[i:end])
            i = end
        elif c in '"\'`':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            body = text[i + 1:j]
            if c == "'":
                body = body.replace("\\'", "'").replace('"', '\\"')
                out.append('"' + body + '"')
            else:
                out.append(text[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def script_phrases():
    """The pages' scripts: the phrases they translate, the count pairs, and any English left outside tr()."""
    used, pairs, loose = set(), set(), []
    for name in sorted(os.listdir(WEB)):
        if not name.endswith('.js') or name in ('sha256.js', 'encoder-worker.js', 'i18n.js', 'lang-pick.js', 'qr.js'):
            continue
        text = double_quoted(open(os.path.join(WEB, name), encoding='utf-8').read())
        inside = set()  # where each phrase given to tr(), trc(), trParts() or trCount() starts; any other string is looked at
        for m in re.finditer(r'\btr(?:Parts)?\(\s*' + STRING, text):
            used.add(unquote(m.group(1)))
            inside.add(m.start(1) - 1)
        for m in re.finditer(r'\btrc\(\s*' + STRING + r'\s*,\s*' + STRING, text):
            used.add(unquote(m.group(1)) + i18n.SEP + unquote(m.group(2)))
            inside.update((m.start(1) - 1, m.start(2) - 1))
        for m in re.finditer(r'\btrCount\(\s*[^,"]+,\s*' + STRING + r'\s*,\s*' + STRING, text):
            pairs.add((unquote(m.group(1)), unquote(m.group(2))))
            inside.update((m.start(1) - 1, m.start(2) - 1))
        for number, line in enumerate(text.split('\n'), 1):
            if '// not words' in line or line.strip().startswith('//'):
                continue
            start = sum(len(x) + 1 for x in text.split('\n')[:number - 1])
            for m in re.finditer(STRING, line):
                if start + m.start() in inside:
                    continue
                s = unquote(m.group(1))
                if looks_like_words(s):
                    loose.append(f'web/{name}:{number}: "{s}"')
    return used, pairs, loose


# Where else the hub's words that the pages show come from, each as (file, the text holding them, which strings there).
# 'all' takes every string in that text that has a word in it; a pattern takes its first group.
HUB_WORDS = [
    ('lib/sensorcam/cam_events.cpp', r'const char\* camEventName\(.*?\n}\n', 'all'),  # each event's name
    ('lib/sensorcam/cam_web_views.cpp', r'const char\* problem = [^;]*;', 'all'),  # a sensor's problem, on the Overview
    ('lib/sensorcam/cam_mqtt_client.cpp', r'.*', r'stop\(' + STRING),  # why Home Assistant's broker is not connected
    ('lib/sensorcam/cam_mqtt_client.cpp', r'std::string CamMqttClient::state\(\) const \{.*?\n}\n', 'all'),
    ('lib/sensorcam/cam_post.cpp', r'.*', r'(?:finish\(|result = )' + STRING),  # how the last notification went
    ('lib/sensorcam/cam_webhook.cpp', r'.*', r'noteResult\(' + STRING),
    ('lib/sensorcam/cam_sensors.cpp', r'const char\* camRestartName\(.*?\n}\n', 'all'),  # why a node restarted
    ('lib/sensorcam/cam_board_rules.cpp', r'RESTART_TEXT\[RESTART_CAUSES\] = \{.*?\};', 'all'),  # why the hub did
    ('lib/sensorcam/cam_alarm.cpp', r'.*', r'refusal = ' + STRING),  # why a message was refused
    ('lib/sensorcam/cam_web_api_system.cpp', r'std::string update = [^;]*;', 'all'),  # a node's update
    ('lib/sensorcam/cam_alarm_arming.cpp', r'.*', r'setArmMode\([^;]*?' + STRING + r'\)'),  # who armed or disarmed
    ('src/main.cpp', r'.*', r'setArmMode\([^;]*?' + STRING + r'\)'),
    ('lib/sensorcam/cam_web_api_alarm.cpp', r'.*', r'setArmMode\(\w+, ' + STRING),
    ('src/hub_restart.cpp', r'const char\* lastRestartText\(\) \{.*?\n}\n', 'all'),
    ('src/device_info.cpp', r'.*', r'\badd\(' + STRING),  # Device info's rows
    ('src/device_info.cpp', r'.*', r'(?:\?|:|String\()\s*' + STRING + r'\s*[):,]'),  # and the values that are words
]


def hub_phrases():
    found = set()
    for name in sorted(os.listdir(LIB)):
        if not name.endswith('.cpp'):
            continue
        text = open(os.path.join(LIB, name), encoding='utf-8').read()
        for m in re.finditer(r'jsonFail\(\s*\d+\s*,\s*' + STRING + r'\s*(?:\)|,\s*\{)', text):  # whole, or with its values
            found.add(unquote(m.group(1)))
        for m in re.finditer(r'\bsaved\([^;]*?,\s*' + STRING + r'\s*\)', text):  # the message when saving fails
            found.add(unquote(m.group(1)))
    for path, where, which in HUB_WORDS:
        text = open(os.path.join(ROOT, path), encoding='utf-8').read()
        parts = [m.group(0) for m in re.finditer(where, text, re.S)]
        if not parts or parts == ['']:
            print(f'{path}: found nothing like {where}; tools/check_translations.py needs to know where those words went')
            found.add('\0missing')
        for part in parts:
            if which == 'all':
                found.update(unquote(s) for s in re.findall(STRING, part) if has_letters(unquote(s)))
            else:
                found.update(unquote(m.group(1)) for m in re.finditer(which, part))
    # A piece of a longer message, which the pages match whole or by a pattern with {name}, is left out.
    return {f for f in found if f not in SAME and not re.match(r'^[A-Z_]+$', f) and not re.search(r'^[\s,;-]|[\s,:]$', f)}


# The hub's own messages, notifications and the daily check, built from whole sentences by camText() and camCount() in
# its C++, and the words it looks up as they come: event names, problems, why it restarted and who armed it.
MESSAGE_WORDS = [
    ('lib/sensorcam/cam_events.cpp', r'const char\* camEventName\(.*?\n}\n', 'all'),
    ('lib/sensorcam/cam_web_views.cpp', r'const char\* problem = [^;]*;', 'all'),
    ('lib/sensorcam/cam_board_rules.cpp', r'RESTART_TEXT\[RESTART_CAUSES\] = \{.*?\};', 'all'),
    ('src/hub_restart.cpp', r'const char\* lastRestartText\(\) \{.*?\n}\n', 'all'),
    ('lib/sensorcam/cam_alarm_arming.cpp', r'.*', r'setArmMode\([^;]*?' + STRING + r'\)'),
    ('src/main.cpp', r'.*', r'setArmMode\([^;]*?' + STRING + r'\)'),
    ('lib/sensorcam/cam_web_api_alarm.cpp', r'.*', r'setArmMode\(\w+, ' + STRING),
    ('lib/sensorcam/cam_mqtt_client.cpp', r'.*', r'setArmMode\(\w+, ' + STRING),
]


def message_phrases():
    used, pairs = set(), set()
    for folder in (LIB, os.path.join(ROOT, 'src')):
        for name in sorted(os.listdir(folder)):
            if not name.endswith('.cpp') or name in ('cam_text_data.cpp', 'cam_web_pages.cpp'):
                continue
            text = open(os.path.join(folder, name), encoding='utf-8').read()
            for m in re.finditer(r'\bcamText\(', text):
                used.update(phrases_in(call_text(text, m.end())))
            for m in re.finditer(r'\bcamCount\([^,"]+,\s*' + STRING + r'\s*,\s*' + STRING, text):
                pairs.add((unquote(m.group(1)), unquote(m.group(2))))
    used |= words_from(MESSAGE_WORDS)
    return used, pairs


def call_text(text, start):
    """A call's arguments, from just after its "(" to its own ")", strings and nested calls included."""
    depth, i = 1, start
    while i < len(text) and depth:
        if text[i] == '"':
            i += 1
            while text[i] != '"':
                i += 2 if text[i] == '\\' else 1
        elif text[i] in '({':
            depth += 1
        elif text[i] in ')}':
            depth -= 1
        i += 1
    return text[start:i - 1]


def phrases_in(args):
    """The phrases in camText()'s arguments: each string with words in it, adjacent strings joined as C++ joins them,
    both sides of a ?: among them; not the values, from the {.
    """
    first, i = args, 0
    while i < len(args):  # the values start at the first { outside a string
        if args[i] == '"':
            i += 1
            while args[i] != '"':
                i += 2 if args[i] == '\\' else 1
        elif args[i] == '{':
            first = args[:i]
            break
        i += 1
    found = set()
    for run in re.finditer(r'(?:' + STRING + r'\s*)+', first):
        if re.search(r'[=!]=\s*$', first[:run.start()]):
            continue  # compared with, as mode == "home": not a phrase
        s = ''.join(unquote(x) for x in re.findall(STRING, run.group(0)))
        if has_letters(s) or '{' in s:
            found.add(s)
    return found


def words_from(rules):
    found = set()
    for path, where, which in rules:
        text = open(os.path.join(ROOT, path), encoding='utf-8').read()
        parts = [m.group(0) for m in re.finditer(where, text, re.S)]
        if not parts or parts == ['']:
            print(f'{path}: found nothing like {where}; tools/check_translations.py needs to know where those words went')
            found.add('\0missing')
        for part in parts:
            if which == 'all':
                found.update(unquote(s) for s in re.findall(STRING, part) if has_letters(unquote(s)))
            else:
                found.update(unquote(m.group(1)) for m in re.finditer(which, part))
    return {f for f in found if f not in SAME and not re.match(r'^[A-Z_]+$', f) and not re.search(r'^[\s,;-]|[\s,:]$', f)}


def names_in(s):
    return set(re.findall(r'\{\w+\}', s))


def check(cat, pages, scripts, page_pairs, hub, messages, message_pairs):
    """What is wrong with one language: each phrase missing or fuzzy, each count pair without its plural, each phrase no
    longer used, and each translation whose {names} differ from its English."""
    problems = []
    entries = cat.by_key()
    where = f'i18n/{cat.code}.po'

    def need(key, what):
        e = entries.get(key)
        if e is None:
            problems.append(f'{where}: no {what} "{key.replace(i18n.SEP, " | ")}"')
        elif not e.translated():
            problems.append(f'{where}: "{key.replace(i18n.SEP, " | ")}" is {"fuzzy" if e.fuzzy else "not translated"}')

    for p in sorted(pages):
        need(p, 'phrase of web/*.html')
    for p in sorted(scripts):
        need(p, 'phrase of web/*.js')
    for p in sorted(hub):
        need(p, 'hub message the pages show')
    for p in sorted(messages):
        need(i18n.MESSAGE + i18n.SEP + p, 'phrase of the hub\'s own messages')
    for one, many, ctx in [(o, m, None) for o, m in page_pairs] + [(o, m, i18n.MESSAGE) for o, m in message_pairs]:
        e = entries.get((ctx + i18n.SEP if ctx else '') + one)
        if e is None or e.plural != many:
            problems.append(f'{where}: no plural entry for "{one}" / "{many}"' + (' in the context "message"' if ctx else ''))
        elif len(e.forms) != cat.nplurals or not all(e.forms):
            problems.append(f'{where}: "{one}" needs its {cat.nplurals} plural forms')
    used = pages | scripts | hub | {o for o, _ in page_pairs} | {i18n.MESSAGE + i18n.SEP + p for p in messages}
    used |= {i18n.MESSAGE + i18n.SEP + o for o, _ in message_pairs}
    for e in cat.entries:
        # A page's phrase with {name} that no script asks for is a pattern, matched as the hub's messages arrive.
        pattern = e.ctx is None and not e.plural and names_in(e.msgid)
        if e.key not in used and not pattern and e.msgid not in SAME:
            problems.append(f'{where}: "{e.key.replace(i18n.SEP, " | ")}" is not used any more')
        english = names_in(e.msgid) | names_in(e.plural or '')
        for t in e.forms if e.plural else [e.msgstr]:
            if t and names_in(t) != english and names_in(t) != names_in(e.msgid):
                problems.append(f'{where}: "{e.msgid}" and its translation have different {{names}}')
    return problems


def main():
    cats = i18n.catalogs()
    pages = page_phrases()
    scripts, page_pairs, loose = script_phrases()
    hub = hub_phrases()
    messages, message_pairs = message_phrases()
    problems = [f'{x}: English outside tr(), or mark the line // not words' for x in loose]
    if '\0missing' in hub | messages:
        problems.append('a source of the hub\'s words was not found')
    hub.discard('\0missing')
    messages.discard('\0missing')
    for cat in cats.values():
        found = check(cat, pages, scripts, page_pairs, hub, messages, message_pairs)
        if cat.complete:
            problems += found
        else:
            print(f'i18n/{cat.code}.po: not offered yet (X-Complete is not yes); {len(found)} thing(s) to do')
    for p in problems:
        print(p)
    shown = [c.code for c in cats.values() if c.complete]
    print(f'every phrase translated, in {", ".join(shown) or "no language but English"}' if not problems
          else f'{len(problems)} problem(s) with the translations')
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
