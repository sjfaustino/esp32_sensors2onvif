"""PlatformIO pre-build script: names the firmware after the git commit it is built from.

Writes include/version.h (not kept in git), and only when the name changes, so an unchanged
build stays fast. A tag such as v1.2.0 gives "v1.2.0"; without one, the commit, such as "c336d03".
"-modified" means the build had changes that were not committed.
"""
import os
import subprocess

Import('env')  # noqa: F821  (provided by PlatformIO)

root = env.subst('$PROJECT_DIR')  # noqa: F821
try:
    version = subprocess.run(['git', 'describe', '--tags', '--always', '--dirty=-modified'], cwd=root,
                             capture_output=True, text=True, check=True, timeout=20).stdout.strip() or 'unknown'
except (OSError, subprocess.SubprocessError):
    version = 'unknown'

text = ('// Written by tools/version.py on every build; not kept in git.\n'
        '#pragma once\n'
        f'#define FIRMWARE_VERSION "{version}"\n')
path = os.path.join(root, 'include', 'version.h')
try:
    with open(path, encoding='utf8') as f:
        same = f.read() == text
except OSError:
    same = False
if not same:
    with open(path, 'w', encoding='utf8', newline='\n') as f:
        f.write(text)
print('Firmware version:', version)
