"""PlatformIO pre-build script: names the firmware after the git commit it is built from.

Writes include/version.h (not kept in git), and only when the name changes, so an unchanged
build stays fast. A tag such as v1.2.0 gives "v1.2.0"; without one, the commit, such as "c336d03".
"-modified" means the build had changes that were not committed.

After each build it also keeps a copy of firmware.elf in builds/ (not kept in git), named after the
board and version, because reading a crash report needs the .elf of the exact build that crashed.
The newest KEEP_ELF copies per board are kept.
"""
import glob
import os
import shutil
import subprocess

KEEP_ELF = 10

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


def keep_elf(source, target, env):  # noqa: ARG001  (PlatformIO's callback signature; target is the linked .elf)
    folder = os.path.join(root, 'builds')
    os.makedirs(folder, exist_ok=True)
    board = env.subst('$PIOENV')
    shutil.copyfile(str(target[0]), os.path.join(folder, f'{board}-{version}.elf'))
    saved = sorted(glob.glob(os.path.join(folder, f'{board}-*.elf')), key=os.path.getmtime, reverse=True)
    for old in saved[KEEP_ELF:]:
        os.remove(old)
    print(f'Kept builds/{board}-{version}.elf for reading crash reports')


env.AddPostAction('$BUILD_DIR/${PROGNAME}.elf', keep_elf)  # noqa: F821
