"""Checks that the pages' stylesheets name every colour by its role, as a variable set in their :root blocks, light and
dark, so a colour added later cannot leave dark mode behind. A few colours read the same in both and may be written in
place: the floor plan's spots, a sensor's week, the battery chart's line, and white text on a coloured button.
Run by CI; exits 1 with each colour written elsewhere."""

import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXED = re.compile(r'^(\.spot(\.\w+)?|table\.week td\.hit|svg\.chart \.line|button\.primary|@keyframes pulse|to)$')
COLOUR = re.compile(r'#[0-9a-fA-F]{3,8}\b|\brgba?\(|(?<![\w-])(white|black|blue|red|green|gray|grey)(?![\w-])')

problems = []
for name in ('index.css', 'setup.css'):
    text = open(os.path.join(ROOT, 'web', name), encoding='utf-8').read()
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    stack = []  # the selectors of the blocks around the current line
    for number, line in enumerate(text.split('\n'), 1):
        opened = line.strip().endswith('{')
        if opened:
            stack.append(line.strip()[:-1].strip())
            continue
        if line.strip() == '}':
            if stack:
                stack.pop()
            continue
        inner = stack[-1] if stack else ''
        if ':root' in inner or not COLOUR.search(line):
            continue
        if not FIXED.match(inner):
            problems.append(f'web/{name}:{number}: {line.strip()}  (in {inner}): name it in :root, light and dark')
for p in problems:
    print(p)
print('every colour is named' if not problems else f'{len(problems)} colour(s) written in place')
sys.exit(1 if problems else 0)
