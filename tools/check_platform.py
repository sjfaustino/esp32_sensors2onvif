"""Checks that the PlatformIO platform the firmware is built with is the one recorded here, byte for byte, before CI
builds or releases anything. platformio.ini names it by a release URL only; if that file were ever replaced, every build
would quietly take the new one, compiler and all. The platform names each package it installs, the toolchains and the
Arduino framework, by its exact version and URL, so the checksum here fixes those too, unless a published package
file is itself replaced in place, which only hashing each of them, hundreds of MB, would catch.

    python tools/check_platform.py [platformio.ini]   # the hub's by default; the node project passes its own

When the platform is updated on purpose, put the new URL in platformio.ini and the new SHA-256 below, from:
    curl -sL <url> | sha256sum
"""
import hashlib
import os
import re
import sys
import urllib.request

KNOWN = {
    'https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip':
        '313625fe62ef4a7a0bfb9fee40cf261254302a24445bad7d6fc34c2012de67a6',
}


def main():
    ini = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'platformio.ini')
    urls = sorted(set(re.findall(r'^platform\s*=\s*(https://\S+)', open(ini, encoding='utf-8').read(), re.M)))
    if not urls:
        sys.exit(f'{ini} names no platform by URL')
    failed = False
    for url in urls:
        if url not in KNOWN:
            print(f'{url}: not recorded in tools/check_platform.py; add its SHA-256 there once you trust it')
            failed = True
            continue
        with urllib.request.urlopen(url, timeout=120) as r:
            got = hashlib.sha256(r.read()).hexdigest()
        if got != KNOWN[url]:
            print(f'{url}: its SHA-256 is {got}, not the {KNOWN[url]} recorded: the file was replaced')
            failed = True
        else:
            print(f'{url}: as recorded')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
