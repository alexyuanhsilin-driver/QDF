"""Runs the hand-written cases in cases/ ("c expect: true|false" in each file)
against both solver builds and the brute-force reference.

usage: python3 run_cases.py [solver ...]
"""
import glob
import os
import subprocess
import sys

import dqdimacs

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT = [os.path.join(HERE, '..', 'c', 'discqbf'), os.path.join(HERE, '..', 'cpp', 'discqbf')]


def main():
    solvers = sys.argv[1:] or [s for s in DEFAULT if os.path.exists(s)]
    fails = 0
    files = sorted(glob.glob(os.path.join(HERE, 'cases', '*.dqdimacs')))
    for path in files:
        with open(path) as f:
            expect = next(l.split(':')[1].strip() for l in f if l.startswith('c expect:')) == 'true'
        got = {'reference': dqdimacs.evaluate(*dqdimacs.read(path))}
        for s in solvers:
            rc = subprocess.run([s, path], capture_output=True).returncode
            got[os.path.relpath(s, HERE)] = {10: True, 20: False}.get(rc, f'exit {rc}')
        bad = {k: v for k, v in got.items() if v != expect}
        status = 'ok  ' if not bad else 'FAIL'
        fails += bool(bad)
        print(f"{status} {os.path.basename(path):45s} expect {str(expect).lower():5s}"
              + (f"  got {bad}" if bad else ''))
    print(f'{len(files)} cases, {fails} failure(s)')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
