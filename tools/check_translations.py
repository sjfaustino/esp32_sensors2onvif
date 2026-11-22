"""Checks that every phrase of the pages has its Portuguese in web/pt.js, so a page never shows English in the middle of
Portuguese. Run with the other quick checks (tools/precheck.py), by CI and the pre-push hook. It fails with each phrase
missing, each one no page uses any more, and each piece of English in a script that does not go through tr().

The phrases are: the text and the placeholder, title, alt and aria-label words of web/index.html and web/setup.html;
the first argument of each tr() in the scripts, and the two phrases of each trCount(); and the hub's own messages,
each jsonFail() with its whole message in one string, and each event's name, which the pages show as the hub sends
them. A phrase of the hub's that changes, such as one with a sensor's name, is in pt.js with {name} for that part.

A string in a script that reads like English, a capital and a space, or three words, or words ending a sentence, must be
inside tr() or trCount(), or on a line marked // not words, as for a code or a name.
"""
import json
import os
import re
import sys
from html.parser import HTMLParser

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


def table():
    text = double_quoted(open(os.path.join(WEB, 'pt.js'), encoding='utf-8').read())
    found = {}
    # Prettier leaves a one-word key unquoted, and may put a long value on the next line.
    for m in re.finditer(r'^\s*(?:' + STRING + r'|([A-Za-z_$][\w$]*))\s*:\s*' + STRING + r',?\s*$', text, re.M):
        key = unquote(m.group(1)) if m.group(1) is not None else m.group(2)
        if key in found:
            print(f'web/pt.js: "{key}" is there twice')
            found['\0dup'] = True
        found[key] = unquote(m.group(3))
    return found


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
    used, loose = set(), []
    for name in sorted(os.listdir(WEB)):
        if not name.endswith('.js') or name in ('pt.js', 'sha256.js', 'encoder-worker.js', 'i18n.js'):
            continue
        text = double_quoted(open(os.path.join(WEB, name), encoding='utf-8').read())
        inside = set()  # where each phrase given to tr(), trParts() or trCount() starts; any other string is looked at
        for m in re.finditer(r'\btr(?:Parts)?\(\s*' + STRING, text):
            used.add(unquote(m.group(1)))
            inside.add(m.start(1) - 1)
        for m in re.finditer(r'\btrCount\(\s*[^,"]+,\s*' + STRING + r'\s*,\s*' + STRING, text):
            used.update((unquote(m.group(1)), unquote(m.group(2))))
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
    return used, loose


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
        for m in re.finditer(r'jsonFail\(\s*\d+\s*,\s*' + STRING + r'\s*\)', text):
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


def main():
    pt = table()
    problems = ['web/pt.js has a phrase twice'] if pt.pop('\0dup', None) else []
    pages = page_phrases()
    scripts, loose = script_phrases()
    hub = hub_phrases()
    for p in sorted(pages - pt.keys()):
        problems.append(f'web/*.html: no Portuguese for "{p}"')
    for p in sorted(scripts - pt.keys()):
        problems.append(f'web/*.js: no Portuguese for "{p}"')
    for p in sorted(hub - pt.keys()):
        problems.append(f'the hub: no Portuguese for "{p}"')
    problems += [f'{x}: English outside tr(), or mark the line // not words' for x in loose]
    used = pages | scripts | hub
    for k in sorted(pt.keys() - used):
        if not re.search(r'\{\w+\}', k):  # a pattern of the hub's, matched as messages arrive
            problems.append(f'web/pt.js: "{k}" is not used any more')
    for k, v in pt.items():
        if set(re.findall(r'\{\w+\}', k)) != set(re.findall(r'\{\w+\}', v)):
            problems.append(f'web/pt.js: "{k}" and its Portuguese have different {{names}}')
    for p in problems:
        print(p)
    print(f'{len(pt)} phrases, each in Portuguese' if not problems else f'{len(problems)} problem(s) with the translation')
    sys.exit(1 if problems else 0)


if __name__ == '__main__':
    main()
