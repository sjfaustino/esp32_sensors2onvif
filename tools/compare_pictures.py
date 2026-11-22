"""Compares the pictures of the pages in docs/images, as tools/screenshots.py has just taken them again, with the ones
committed, and fails when a page looks different, so a change to a page cannot leave the user guide's pictures behind.
CI runs it after taking the pictures.

  python tools/compare_pictures.py [--against <git revision>]   (HEAD unless given)

A picture counts as changed when more than LIMIT of its pixels differ by more than a shade, or its size differs: the
times and figures in the pictures change a few pixels from run to run, and a changed page far more. The differing
pixels of each changed picture are drawn in red over it in .pio/picture-changes/, which CI keeps. A page changed on
purpose: run tools/screenshots.py and commit the pictures. Needs Pillow.
"""
import io
import os
import subprocess
import sys

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGES = os.path.join(ROOT, 'docs', 'images')
OUT = os.path.join(ROOT, '.pio', 'picture-changes')
LIMIT = 0.001  # the share of a picture's pixels that may differ: two runs differ by under 0.01%, a new menu icon by 0.4%
SHADE = 40  # how far a pixel's channels may differ and still count as the same


def committed(name, revision):
    r = subprocess.run(['git', 'show', f'{revision}:docs/images/{name}'], cwd=ROOT, capture_output=True)
    return Image.open(io.BytesIO(r.stdout)).convert('RGB') if r.returncode == 0 else None


def main(args):
    revision = args[args.index('--against') + 1] if '--against' in args else 'HEAD'
    os.makedirs(OUT, exist_ok=True)
    changed = []
    for name in sorted(n for n in os.listdir(IMAGES) if n.endswith('.png')):
        now = Image.open(os.path.join(IMAGES, name)).convert('RGB')
        before = committed(name, revision)
        if before is None:
            print(f'{name}: new')
            continue
        if before.size != now.size:
            changed.append(f'{name}: {before.size[0]}x{before.size[1]} before, {now.size[0]}x{now.size[1]} now')
            now.save(os.path.join(OUT, name))
            continue
        diff = ImageChops.difference(before, now).convert('L').point(lambda v: 255 if v > SHADE else 0)
        share = diff.histogram()[255] / (now.size[0] * now.size[1])
        print(f'{name}: {share:.3%} of its pixels differ')
        if share > LIMIT:
            changed.append(f'{name}: {share:.2%} of its pixels differ')
            marked = now.copy()
            marked.paste((220, 0, 0), mask=diff)
            marked.save(os.path.join(OUT, name))
    if changed:
        print('\nThese pages look different from their pictures in docs/images. If they changed on purpose, run')
        print('python tools/screenshots.py and commit the pictures; the changes are marked in .pio/picture-changes.')
        for c in changed:
            print('  ' + c)
        return 1
    print('every picture matches its page')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
