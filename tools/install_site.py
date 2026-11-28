"""Builds the install page's site from a release's files, for .github/workflows/pages.yml.

    python tools/install_site.py v0.8.0 ASSETS OUT

ASSETS holds the release's -full.bin files and its SHA256SUMS, as `gh release download` leaves them. OUT gets
site/index.html with the release named in it, each board's -full.bin, checked against SHA256SUMS first, and a
manifest for each that ESP Web Tools reads: the whole board written from offset 0, as a new hub is set up. The
files are copied because a page cannot fetch a release's downloads from another site.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Each board the release workflow builds: its PlatformIO environment, and the chip ESP Web Tools checks it is put on.
BOARDS = {'esp32s3': 'ESP32-S3', 'esp32_16mb': 'ESP32', 'esp32dev': 'ESP32'}


def main(tag, assets, out):
    assets, out = Path(assets), Path(out)
    sums = {}
    for line in (assets / 'SHA256SUMS').read_text(encoding='utf-8').splitlines():
        digest, _, name = line.partition('  ')
        sums[name.strip()] = digest.strip()
    out.mkdir(parents=True, exist_ok=True)
    for board, chip in BOARDS.items():
        name = f'sensors2onvif-{tag}-{board}-full.bin'
        data = (assets / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != sums.get(name):
            sys.exit(f'{name} does not match the release\'s SHA256SUMS')
        (out / name).write_bytes(data)
        manifest = {'name': 'sensors2onvif hub', 'version': tag, 'new_install_prompt_erase': True,
                    'builds': [{'chipFamily': chip, 'parts': [{'path': name, 'offset': 0}]}]}
        (out / f'manifest-{board}.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    page = (ROOT / 'site' / 'index.html').read_text(encoding='utf-8')
    page = page.replace('<b id="version">RELEASE</b>', f'<b id="version">{tag}</b>')
    page = page.replace('href="https://github.com/sjfaustino/esp32_sensors2onvif/releases"',
                        f'href="https://github.com/sjfaustino/esp32_sensors2onvif/releases/tag/{tag}"')
    (out / 'index.html').write_text(page, encoding='utf-8')
    print('site for', tag, 'in', out)


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
