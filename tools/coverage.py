"""Measures how much of the hub's library the tests run: the unit tests and the end-to-end test of the PC build, and
with --pages the pages in a browser too, together, as CI does on each push.

  python tools/coverage.py [--pages]

Builds into .pio/coverage, apart from the usual builds, with gcc's --coverage, and writes:
  .pio/coverage/report/index.html   each file, line by line
  .pio/coverage/summary.md          the totals per file, which CI shows with the run
Needs gcovr (pip install gcovr) and the gcov that comes with the g++ the PC build uses.
"""
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, '.pio', 'coverage')


def run(args, env=None):
    print('>', ' '.join(args), flush=True)
    return subprocess.run(args, cwd=ROOT, env=env).returncode


def main():
    pages = '--pages' in sys.argv
    env = dict(os.environ, PLATFORMIO_BUILD_DIR=os.path.join(OUT, 'build'),
               PLATFORMIO_BUILD_FLAGS='--coverage -lgcov -DHOST_COVERAGE')  # -l goes to the link
    for old in glob.glob(os.path.join(OUT, '**', '*.gcda'), recursive=True):
        os.remove(old)
    failed = run(['pio', 'test', '-e', 'native'], env)
    failed |= run(['pio', 'run', '-e', 'host'], env)
    env['E2E_EXE'] = os.path.join(OUT, 'build', 'host', 'program.exe')
    failed |= run([sys.executable, 'tools/e2e_test.py'], env)
    if pages:
        failed |= run([sys.executable, 'tools/e2e_pages.py'], env)

    os.makedirs(os.path.join(OUT, 'report'), exist_ok=True)
    summary = os.path.join(OUT, 'summary.json')
    if run([sys.executable, '-m', 'gcovr', '--root', '.', '--object-directory', os.path.join(OUT, 'build'),
            '--filter', 'lib/sensorcam/', '--filter', 'lib/s2o_proto/', '--exclude', '.*cam_web_pages\\.cpp', '--exclude', '.*cam_text_data\\.cpp',
            '--gcov-ignore-errors', 'no_working_dir_found', '--merge-mode-functions', 'separate',
            '--html-details', os.path.join(OUT, 'report', 'index.html'), '--json-summary', summary,
            '--txt-summary']):
        return 1
    write_markdown(json.load(open(summary)), os.path.join(OUT, 'summary.md'), pages)
    return 1 if failed else 0


def write_markdown(s, path, pages):
    tests = 'unit tests, the end-to-end test and the pages in a browser' if pages else 'unit tests and the end-to-end test'
    lines = [f"### Coverage of the hub's library: {s['line_percent']:.1f}% of lines, {s['branch_percent']:.1f}% of branches",
             '', f'Measured over the {tests}.', '', '| File | Lines | Branches |', '| --- | ---: | ---: |']
    for f in sorted(s['files'], key=lambda f: f['filename']):
        branches = f"{f['branch_percent']:.1f}% of {f['branch_total']}" if f['branch_total'] else 'none'
        lines.append(f"| `{f['filename']}` | {f['line_percent']:.1f}% of {f['line_total']} | {branches} |")
    with open(path, 'w', encoding='utf-8') as out:
        out.write('\n'.join(lines) + '\n')
    print(f"line coverage {s['line_percent']:.1f}%, branch coverage {s['branch_percent']:.1f}%; {path}")


if __name__ == '__main__':
    sys.exit(main())
