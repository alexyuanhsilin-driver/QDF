"""Cross-check on discrete QBFs: DiscQBF on the formula vs. DepQBF on its
Boolean encoding (encode.py).

usage: python3 vs_depqbf_encoded.py PATH_TO_DEPQBF [-n COUNT] [--seed S]
           [--vars N] [--dom D] [--clauses R] [--reference] [--cases]

--reference  also run the brute-force evaluator (small formulas only)
--cases      check tests/cases/*.dqdimacs instead of random formulas
"""
import argparse
import glob
import os
import random
import subprocess
import sys

import dqdimacs
import encode
import fuzz

HERE = os.path.dirname(os.path.abspath(__file__))
SOLVERS = [os.path.join(HERE, '..', 'c', 'discqbf'),
           os.path.join(HERE, '..', 'cpp', 'discqbf')]


def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'timeout'
    return {10: True, 20: False}.get(p.returncode, f'exit {p.returncode}: {p.stderr.strip()[:100]}')


def check(f, depqbf, solvers, workdir, timeout, reference):
    """Returns (expected, problems) for formula f."""
    src = os.path.join(workdir, 'current.dqdimacs')
    enc = os.path.join(workdir, 'current.qdimacs')
    with open(src, 'w') as out:
        dqdimacs.write(out, *f)
    with open(enc, 'w') as out:
        out.write(encode.encode(*f))
    expected = run([depqbf, enc], timeout)
    if expected == 'timeout':
        return expected, []
    got = {os.path.relpath(s, HERE): run([s, src], timeout) for s in solvers}
    if reference:
        got['brute force'] = dqdimacs.evaluate(*f)
    return expected, [f'{k}: DepQBF(encoded) says {expected}, got {v}'
                      for k, v in got.items() if v != expected]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('depqbf')
    ap.add_argument('-n', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--vars', type=int, default=30)
    ap.add_argument('--dom', type=int, default=5)
    ap.add_argument('--clauses', type=float, default=4)
    ap.add_argument('--timeout', type=float, default=60)
    ap.add_argument('--reference', action='store_true')
    ap.add_argument('--cases', action='store_true')
    ap.add_argument('--keep', default=os.path.join(HERE, 'failures'))
    args = ap.parse_args()

    solvers = [s for s in SOLVERS if os.path.exists(s)]
    if not solvers:
        sys.exit('no solver binaries found; build c/ and cpp/ first')
    os.makedirs(args.keep, exist_ok=True)
    if args.cases:
        paths = sorted(glob.glob(os.path.join(HERE, 'cases', '*.dqdimacs')))
        formulas = [(os.path.basename(p), dqdimacs.read(p)) for p in paths]
    else:
        rng = random.Random(args.seed)
        formulas = ((f'{args.seed}_{i}', fuzz.random_formula(rng, args.vars, args.dom, args.clauses))
                    for i in range(args.n))

    fails, counts = 0, {True: 0, False: 0, 'timeout': 0}
    total = 0
    for name, f in formulas:
        total += 1
        expected, problems = check(f, args.depqbf, solvers, args.keep, args.timeout,
                                   args.reference or args.cases)
        counts[expected] = counts.get(expected, 0) + 1
        if problems:
            fails += 1
            path = os.path.join(args.keep, f'encoded_fail_{name}.dqdimacs')
            with open(path, 'w') as out:
                dqdimacs.write(out, *f)
            for p in problems:
                print(f'[{name}] {p} -> {path}')
    for tmp in ('current.dqdimacs', 'current.qdimacs'):
        if os.path.exists(os.path.join(args.keep, tmp)):
            os.remove(os.path.join(args.keep, tmp))
    print(f'{total} formulas ({counts[True]} true, {counts[False]} false, '
          f'{counts["timeout"]} DepQBF timeouts), {fails} failure(s)')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
