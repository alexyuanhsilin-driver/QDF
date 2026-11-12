"""Random differential testing: brute force vs. the C and C++ solvers.

usage: python3 fuzz.py [-n COUNT] [--seed S] [--vars N] [--dom D] [--keep DIR]

Each round generates a small random discrete QBF, evaluates it with the
brute-force reference and runs every solver binary found. Mismatches, crashes
and timeouts are saved to the --keep directory (default: failures/).
"""
import argparse
import os
import random
import subprocess
import sys

import dqdimacs

HERE = os.path.dirname(os.path.abspath(__file__))
SOLVERS = [os.path.join(HERE, '..', 'c', 'discqbf'),
           os.path.join(HERE, '..', 'cpp', 'discqbf')]


def random_formula(rng, max_vars, max_dom, clause_ratio=4):
    n = rng.randint(1, max_vars)
    domains = {x: rng.choice([1, 2, 2, 2, 3, 3, 4, 5, max_dom]) for x in range(1, n + 1)}
    vs = list(range(1, n + 1))
    rng.shuffle(vs)
    free = rng.randint(0, n // 3) if rng.random() < 0.3 else 0
    quantified = vs[free:]
    nblocks = rng.randint(1, max(1, min(5, len(quantified))))
    cuts = sorted(rng.sample(range(1, len(quantified)), nblocks - 1)) if len(quantified) > 1 else []
    q = rng.choice('ea')
    prefix, prev = [], 0
    for c in cuts + [len(quantified)]:
        if c > prev:
            prefix.append((q, sorted(quantified[prev:c])))
            q = 'a' if q == 'e' else 'e'
        prev = c
    m = rng.randint(1, int(clause_ratio * n) + 2)
    clauses = []
    for _ in range(m):
        k = rng.choice([1, 2, 2, 3, 3, 3, 4])
        c = []
        for _ in range(k):
            x = rng.randint(1, n)
            d = domains[x]
            s = frozenset(v for v in range(d) if rng.random() < 0.4)
            if not s:
                s = frozenset([rng.randrange(d)])
            c.append((x, s))
        clauses.append(c)
    return domains, prefix, clauses


def run(solver, path, timeout, extra=()):
    """Returns (result, stats) with result True/False or an error string."""
    try:
        p = subprocess.run([solver, '-v', *extra, path], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'timeout', None
    stats = [l for l in p.stderr.splitlines() if l.startswith('c ')]
    if p.returncode == 10:
        return True, stats
    if p.returncode == 20:
        return False, stats
    return f'crash (exit {p.returncode}): {p.stderr.strip()[:200]}', None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', type=int, default=500)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--vars', type=int, default=8)
    ap.add_argument('--dom', type=int, default=6)
    ap.add_argument('--timeout', type=float, default=10)
    ap.add_argument('--keep', default=os.path.join(HERE, 'failures'))
    ap.add_argument('--solver', action='append', help='solver binary (default: c/ and cpp/ builds)')
    ap.add_argument('--no-reference', action='store_true',
                    help='skip brute force (for large formulas); only compare solvers with each other')
    ap.add_argument('--clauses', type=float, default=4,
                    help='max clauses per variable')
    ap.add_argument('--stress', action='store_true',
                    help='restart after every conflict and keep very few learned constraints')
    args = ap.parse_args()
    args.solver_args = ['--restart-base=1', '--max-learned=4'] if args.stress else []

    solvers = args.solver or [s for s in SOLVERS if os.path.exists(s)]
    if not solvers:
        sys.exit('no solver binaries found; build c/ and cpp/ first')
    rng = random.Random(args.seed)
    tmp = os.path.join(args.keep, 'current.dqdimacs')
    os.makedirs(args.keep, exist_ok=True)
    fails, counts = 0, {True: 0, False: 0}
    for i in range(args.n):
        f = random_formula(rng, args.vars, args.dom, args.clauses)
        with open(tmp, 'w') as out:
            dqdimacs.write(out, *f)
        results = [run(s, tmp, args.timeout, args.solver_args) for s in solvers]
        expected = results[0][0] if args.no_reference else dqdimacs.evaluate(*f)
        if expected in counts:
            counts[expected] += 1
        problems = [f'{os.path.relpath(s, HERE)}: expected {expected}, got {got}'
                    for s, (got, _) in zip(solvers, results) if got != expected]
        # The C and C++ versions implement identical heuristics: statistics must match too.
        if not problems and any(st != results[0][1] for _, st in results):
            problems.append('statistics differ between solvers: ' +
                            ' vs '.join(str(st) for _, st in results))
        if problems:
            fails += 1
            name = os.path.join(args.keep, f'fail_{args.seed}_{i}.dqdimacs')
            with open(name, 'w') as out:
                dqdimacs.write(out, *f)
            for p in problems:
                print(f'[{i}] {p} -> {name}')
    os.remove(tmp)
    print(f'{args.n} formulas ({counts[True]} true, {counts[False]} false), '
          f'{len(solvers)} solver(s), {fails} failure(s)')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
