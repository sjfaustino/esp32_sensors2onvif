"""Builds the hub's web pages into the firmware.

The pages are ordinary files in web/: each NAME.html links NAME.css and NAME.js. This puts the
stylesheet and script inside the page, gzips it, and writes the bytes to
lib/sensorcam/cam_web_pages.cpp (not kept in git), which cam_web.h declares. The hub sends a page
as it is stored, with Content-Encoding: gzip.

PlatformIO runs it before every build (extra_scripts in platformio.ini). It also runs on its own:
python tools/embed_web.py. The output is written only when it changes, so an unchanged page does
not rebuild anything.
"""
import gzip
import os
import re
import sys

PAGES = [('index', 'CAM_WEB_PAGE'), ('setup', 'CAM_SETUP_PAGE')]


def inline(folder, name):
    def read(file):
        with open(os.path.join(folder, file), encoding='utf-8') as f:
            return f.read()

    page = read(name + '.html')
    css, js = read(name + '.css'), read(name + '.js')
    if '</style' in css.lower() or '</script' in js.lower():
        sys.exit(f'web/{name}: a stylesheet or script cannot contain </style or </script')
    link = f'<link rel="stylesheet" href="{name}.css">'
    script = f'<script src="{name}.js"></script>'
    if page.count(link) != 1 or page.count(script) != 1:
        sys.exit(f'web/{name}.html must contain {link} and {script} once each')
    page = page.replace(link, '<style>\n' + css + '</style>').replace(script, '<script>\n' + js + '</script>')
    leftover = re.findall(r'<(?:link|script)[^>]*(?:href|src)="(?!https?:)[^"]*"', page)
    if leftover:
        sys.exit(f'web/{name}.html links a file the hub does not serve: {leftover[0]}')
    return page


def generate(root):
    folder = os.path.join(root, 'web')
    out = ['// Written by tools/embed_web.py from the files in web/ on every build; not kept in git.',
           '#include "cam_web.h"', '']
    for name, var in PAGES:
        packed = gzip.compress(inline(folder, name).encode('utf-8'), compresslevel=9, mtime=0)
        out.append(f'const uint8_t {var}[] = {{')
        for i in range(0, len(packed), 24):
            out.append('  ' + ','.join(str(b) for b in packed[i:i + 24]) + ',')
        out.append('};')
        out.append(f'const size_t {var}_SIZE = sizeof({var});')
        out.append('')
        print(f'web/{name}.html: {len(packed)} bytes gzipped')
    text = '\n'.join(out)
    path = os.path.join(root, 'lib', 'sensorcam', 'cam_web_pages.cpp')
    try:
        with open(path, encoding='utf-8') as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


try:
    Import('env')  # noqa: F821  (provided by PlatformIO)
except NameError:  # run on its own
    generate(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
else:
    generate(env.subst('$PROJECT_DIR'))  # noqa: F821
