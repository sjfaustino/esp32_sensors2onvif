"""Runs clang-tidy, with the checks in .clang-tidy, over the hub's library and its PC build, as CI does.

  pio run -e host -t compiledb      # first: writes compile_commands.json
  python tools/tidy.py [clang-tidy]  # the clang-tidy program; by default the one on PATH, or LLVM's on Windows

Monocypher, a library by others, is left out. Exits non-zero on any finding.
"""
import json
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tidy_program():
    if len(sys.argv) > 1:
        return sys.argv[1]
    found = shutil.which('clang-tidy')
    if found:
        return found
    llvm = os.path.join(os.environ.get('ProgramFiles', ''), 'LLVM', 'bin', 'clang-tidy.exe')
    if os.path.exists(llvm):
        return llvm
    sys.exit('No clang-tidy: pip install clang-tidy, or install LLVM')


def main():
    os.chdir(ROOT)
    tidy = tidy_program()
    entries = json.load(open('compile_commands.json'))
    files = sorted({os.path.normpath(e['file']) for e in entries})
    files = [f for f in files if 'monocypher' not in f and os.path.splitext(f)[1] == '.cpp']
    extra = []
    if os.name == 'nt':  # the PC build uses MinGW's g++, whose headers clang reads only when told the target
        extra = ['--extra-arg=--target=x86_64-w64-mingw32']

    def run(f):
        r = subprocess.run([tidy, '-p', '.', '--quiet', *extra, f], capture_output=True, text=True)
        return f, r.returncode, r.stdout + r.stderr

    failed = 0
    with ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for f, code, out in pool.map(run, files):
            lines = [line for line in out.splitlines() if 'warnings generated' not in line and 'warning generated' not in line
                     and 'Suppressed' not in line and 'Use -header-filter' not in line and line.strip()]
            if code or lines:
                failed += 1
                print('\n'.join(lines))
    print(f'clang-tidy: {len(files)} files, {failed} with findings')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
