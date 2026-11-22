"""The quick checks CI runs before its tests, run the same way here, so a push that CI would refuse for its style is
caught in seconds on this PC instead of minutes later on GitHub. CI runs this script itself, so the two cannot differ.

    python tools/precheck.py          # every check; exits 1 if any fails
    python tools/precheck.py --fix    # formats the C++ and the pages' scripts and styles first
    python hub/tools/precheck.py --root .   # in the node project: its C++ style, as CI checks it

Run before each push by the hook in tools/hooks, once git is told to use it:

    git config core.hooksPath tools/hooks

Needs clang-format 19.1.7 (pip install clang-format==19.1.7), Node.js for node --check and npx, and Python. A check
whose tool is missing fails, saying what to install, as CI would otherwise be the first to find the problem.
"""
import os
import shutil
import subprocess
import sys

HUB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The project checked: the hub's, or, with --root, another that has the hub in it, such as the sensor node's, which runs
# this from its hub/ folder and gets the checks that apply to it: the C++ style, as it has no pages.
ROOT = HUB
if '--root' in sys.argv:
    ROOT = os.path.abspath(sys.argv[sys.argv.index('--root') + 1])
CLANG_FORMAT = '19.1.7'
PRETTIER = ['prettier@3.3.3', '--print-width', '120', '--end-of-line', 'lf']


def tool(name):
    """The program's path, as Windows needs for npx.cmd; None when it is not installed."""
    return shutil.which(name)


def run(args):
    return subprocess.run(args, cwd=ROOT).returncode == 0


def git_files(*patterns):
    out = subprocess.run(['git', 'ls-files', *patterns], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    return [f for f in out.split('\n') if f]


def cpp_files():
    return [f for f in git_files('*.cpp', '*.h') if not f.endswith('_data.cpp') and not f.startswith('lib/monocypher/')]


def web_files():
    return git_files('web/*.js', 'web/*.css')


def clang_format(fix):
    cf = tool('clang-format')
    if not cf:
        print(f'  clang-format is not installed: pip install clang-format=={CLANG_FORMAT}')
        return False
    version = subprocess.run([cf, '--version'], capture_output=True, text=True).stdout
    if CLANG_FORMAT not in version:
        print(f'  clang-format {CLANG_FORMAT} is what CI uses, and this is {version.strip()}: another may format differently')
        return False
    files = cpp_files()
    return run([cf, '-i', *files]) if fix else run([cf, '--dry-run', '--Werror', *files])


def scripts_parse(fix):
    node = tool('node')
    if not node:
        print('  Node.js is not installed')
        return False
    return all(run([node, '--check', f]) for f in web_files() if f.endswith('.js'))


def prettier(fix):
    npx = tool('npx')
    if not npx:
        print('  npx is not installed: it comes with Node.js')
        return False
    return run([npx, '--yes', *PRETTIER, '--write' if fix else '--check', *web_files()])


def python_check(script):
    return lambda fix: run([sys.executable, os.path.join(HUB, 'tools', script)])


CHECKS = [
    ('C++ style (clang-format)', clang_format),
    ('page scripts parse (node --check)', scripts_parse),
    ('page scripts and styles (Prettier)', prettier),
    ('every setting is loaded and saved', python_check('check_settings.py')),
    ('every colour of the pages is named, light and dark', python_check('check_colours.py')),
    ('every phrase of the pages has its Portuguese', python_check('check_translations.py')),
]


def main():
    fix = '--fix' in sys.argv[1:]
    failed = []
    pages = os.path.isdir(os.path.join(ROOT, 'web'))
    for name, check in CHECKS if pages else CHECKS[:1]:
        print(f'-- {name}', flush=True)
        if not check(fix):
            failed.append(name)
    if failed:
        print('\nfailed: ' + '; '.join(failed))
        print('python tools/precheck.py --fix formats the code; the rest needs a change by hand')
        sys.exit(1)
    print('\nevery quick check passed')


if __name__ == '__main__':
    main()
