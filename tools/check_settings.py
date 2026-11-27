"""Checks that every setting in the table, CAM_SETTINGS in lib/sensorcam/cam_settings.cpp, is both loaded and saved in
src/hub_settings.cpp, so one added to the table cannot be forgotten in either. Run by CI; exits 1 with what is missing.
"""

import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
table = open(os.path.join(ROOT, 'lib', 'sensorcam', 'cam_settings.cpp'), encoding='utf-8').read()
code = open(os.path.join(ROOT, 'src', 'hub_settings.cpp'), encoding='utf-8').read()

start = table.index('const CamSetting CAM_SETTINGS[] = {')
settings = re.findall(r'\{"(\w+)", "(\w+)", (CAM_KIND_\w+)', table[start:table.index('};', start)])
if len(settings) < 30:
    sys.exit(f'found only {len(settings)} settings in CAM_SETTINGS: has its layout changed?')

missing = []
for space, key, kind in settings:
    quoted = re.escape(f'"{key}"')
    loads = re.search(r'\bget\w*\(\s*' + quoted, code) or re.search(r'\bisKey\(\s*' + quoted, code)
    saves = re.search(r'\bput\w*\(\s*' + quoted, code) or re.search(r'\bputText\(\s*p,\s*' + quoted, code)
    if not loads:
        missing.append(f'{space}.{key} is never loaded')
    if not saves:
        missing.append(f'{space}.{key} is never saved')
for line in missing:
    print(line)
print(f'{len(settings)} settings checked' if not missing else f'{len(missing)} problem(s)')
sys.exit(1 if missing else 0)
