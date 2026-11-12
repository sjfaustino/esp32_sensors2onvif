"""Builds the hub's web pages into the firmware.

The pages are ordinary files in web/: each NAME.html links its stylesheets and scripts there. This
puts them inside the page, where they were linked, gzips it, and writes the bytes to
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
    """The page with each stylesheet and script it links from web/ put inside it, in the same place."""
    def read(file):
        with open(os.path.join(folder, file), encoding='utf-8') as f:
            text = f.read()
        if not file.endswith('.html') and ('</style' in text.lower() or '</script' in text.lower()):
            sys.exit(f'web/{file}: a stylesheet or script cannot contain </style or </script')
        return text

    page = read(name + '.html')
    page = re.sub(r'<link rel="stylesheet" href="([\w.-]+\.css)">', lambda m: '<style>\n' + read(m.group(1)) + '</style>', page)
    page = re.sub(r'<script([^>]*?) src="([\w.-]+\.js)"></script>', lambda m: f'<script{m.group(1)}>\n' + read(m.group(2)) + '</script>', page)
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
