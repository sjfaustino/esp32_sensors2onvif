"""Builds the hub's web pages, and their languages, into the firmware.

The pages are ordinary files in web/: each NAME.html links its stylesheets and scripts there. This puts them inside
the page, where they were linked, gzips it, and writes the bytes to lib/sensorcam/cam_web_pages.cpp (not kept in git),
which cam_web.h declares. The hub sends a page as it is stored, with Content-Encoding: gzip, and its ETag.

Each language offered in i18n/ (tools/i18n.py) becomes two files the hub serves at /lang?file=<code> and
/lang?file=<code>-setup: the pages' words, and the setup page's few, as that page goes over the hub's open setup
network, often to a phone. The languages offered go into lang-pick.js's list, and the hub's own messages into
lib/sensorcam/cam_text_data.cpp, also not kept in git.

PlatformIO runs it before every build (extra_scripts in platformio.ini). It also runs on its own:
python tools/embed_web.py. The output is written only when it changes, so an unchanged page does not rebuild anything.
"""
import gzip
import hashlib
import json
import os
import re
import sys

PAGES = [('index', 'CAM_WEB_PAGE'), ('setup', 'CAM_SETUP_PAGE')]
PICKER = 'const LANGUAGES = { en: "English" }; // tools/embed_web.py puts every language offered here'


def tools(root):
    sys.path.insert(0, os.path.join(root, 'tools'))  # PlatformIO runs this with no __file__
    import i18n
    import check_translations
    return i18n, check_translations


def setup_keys(root):
    """The phrases the setup page uses: its own text, its script's, and the messages of the hub's Wi-Fi setting."""
    _, ct = tools(root)
    folder = os.path.join(root, 'web')
    page = ct.Page()
    page.feed(open(os.path.join(folder, 'setup.html'), encoding='utf-8').read())
    keys = set(page.found) | {re.search(r'<title>(.*?)</title>', open(os.path.join(folder, 'setup.html'), encoding='utf-8').read()).group(1)}
    script = ct.double_quoted(open(os.path.join(folder, 'setup.js'), encoding='utf-8').read())
    keys |= {ct.unquote(m.group(1)) for m in re.finditer(r'\btr\(\s*' + ct.STRING, script)}
    api = open(os.path.join(root, 'lib', 'sensorcam', 'cam_web_api_pages.cpp'), encoding='utf-8').read()
    wifi = api[api.index('void CamWeb::postWifi'):]
    keys |= {ct.unquote(s) for s in re.findall(ct.STRING, wifi[:wifi.index('\n}\n')])}
    return keys


def inline(folder, name, languages):
    """The page with each stylesheet and script it links from web/ put inside it, in the same place."""
    def read(file):
        with open(os.path.join(folder, file), encoding='utf-8') as f:
            text = f.read()
        if not file.endswith('.html') and ('</style' in text.lower() or '</script' in text.lower()):
            sys.exit(f'web/{file}: a stylesheet or script cannot contain </style or </script')
        if file == 'lang-pick.js':
            if PICKER not in text:
                sys.exit('web/lang-pick.js: its list of languages is not where tools/embed_web.py puts it')
            text = text.replace(PICKER, 'const LANGUAGES = ' + json.dumps(languages, ensure_ascii=False) + ';')
        return text

    page = read(name + '.html')
    page = re.sub(r'<link rel="stylesheet" href="([\w.-]+\.css)">', lambda m: '<style>\n' + read(m.group(1)) + '</style>', page)
    page = re.sub(r'<script([^>]*?) src="([\w.-]+\.js)"></script>', lambda m: f'<script{m.group(1)}>\n' + read(m.group(2)) + '</script>', page)
    leftover = re.findall(r'<(?:link|script)[^>]*(?:href|src)="(?!https?:)[^"]*"', page)
    if leftover:
        sys.exit(f'web/{name}.html links a file the hub does not serve: {leftover[0]}')
    return page


def c_bytes(name, packed):
    out = [f'static const uint8_t {name}[] = {{']
    for i in range(0, len(packed), 24):
        out.append('  ' + ','.join(str(b) for b in packed[i:i + 24]) + ',')
    out.append('};')
    return out


def tag(packed):
    return '"\\"' + hashlib.sha256(packed).hexdigest()[:16] + '\\""'


def generate(root):
    i18n, _ = tools(root)
    folder = os.path.join(root, 'web')
    cats = [c for c in i18n.catalogs().values() if c.complete]
    languages = {'en': 'English', **{c.code: c.name for c in cats}}
    out = ['// Written by tools/embed_web.py from the files in web/ and i18n/ on every build; not kept in git.',
           '#include "cam_web.h"', '']
    for name, var in PAGES:
        packed = gzip.compress(inline(folder, name, languages).encode('utf-8'), compresslevel=9, mtime=0)
        out += [line.replace('static const', 'const', 1) for line in c_bytes(var, packed)]
        out.append(f'const size_t {var}_SIZE = sizeof({var});')
        out.append(f'const char {var}_TAG[] = {tag(packed)};  // its ETag')
        out.append('')
        print(f'web/{name}.html: {len(packed)} bytes gzipped')
    keep = setup_keys(root) if cats else set()
    files = []
    for c in cats:
        for suffix, words in (('', i18n.page_words(c)), ('-setup', i18n.page_words(c, keep))):
            packed = gzip.compress(words.encode('utf-8'), compresslevel=9, mtime=0)
            var = 'LANG_' + re.sub(r'\W', '_', c.code + suffix)
            out += c_bytes(var, packed)
            files.append(f'    {{"{c.code}{suffix}", {var}, sizeof({var}), {tag(packed)}}},')
            print(f'i18n/{c.code}.po: {c.code}{suffix} {len(packed)} bytes gzipped')
    out.append('const CamLangFile CAM_LANG_FILES[] = {')
    out += files or ['    {"", nullptr, 0, ""},']
    out.append('};')
    out.append(f'const size_t CAM_LANG_FILE_COUNT = {len(files)};')
    i18n.write_if_changed(os.path.join(root, 'lib', 'sensorcam', 'cam_web_pages.cpp'), '\n'.join(out) + '\n')
    i18n.write_if_changed(os.path.join(root, 'lib', 'sensorcam', 'cam_text_data.cpp'), i18n.message_table(cats))


try:
    Import('env')  # noqa: F821  (provided by PlatformIO)
except NameError:  # run on its own
    generate(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
else:
    generate(env.subst('$PROJECT_DIR'))  # noqa: F821
