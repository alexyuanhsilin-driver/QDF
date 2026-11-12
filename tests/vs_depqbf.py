"""Cross-check on Boolean QBFs (all domains 2) against the original DepQBF.

usage: python3 vs_depqbf.py PATH_TO_DEPQBF [-n COUNT] [--seed S] [--vars N]

Every QDIMACS file is also a valid DQDIMACS file, so the same file is given to
DepQBF and to both DiscQBF builds. Instances can be far larger than what the
brute-force reference handles.
"""
import argparse
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOLVERS = [os.path.join(HERE, '..', 'c', 'discqbf'),
           os.path.join(HERE, '..', 'cpp', 'discqbf')]


def random_qdimacs(rng, max_vars):
    n = rng.randint(10, max_vars)
    vs = list(range(1, n + 1))
    rng.shuffle(vs)
    nblocks = rng.randint(2, 5)
    cuts = sorted(rng.sample(range(1, n), nblocks - 1))
    q = rng.choice('ea')
    prefix, prev = [], 0
    for c in cuts + [n]:
        prefix.append((q, sorted(vs[prev:c])))
        q = 'a' if q == 'e' else 'e'
        prev = c
    univ = {x for q, b in prefix if q == 'a' for x in b}
    m = int(n * rng.uniform(0.6, 2.5))
    clauses = []
    for _ in range(m):
        c = rng.sample(range(1, n + 1), min(n, rng.choice([3, 3, 4, 4, 5])))
        if all(x in univ for x in c):  # avoid clauses that reduce to empty
            continue
        clauses.append([x if rng.random() < 0.5 else -x for x in c])
    lines = [f'p cnf {n} {len(clauses)}']
    lines += [f"{q} {' '.join(map(str, b))} 0" for q, b in prefix]
    lines += [' '.join(map(str, c)) + ' 0' for c in clauses]
    return '\n'.join(lines) + '\n'


def run(cmd, timeout):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return 'timeout'
    return {10: True, 20: False}.get(p.returncode, f'exit {p.returncode}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('depqbf')
    ap.add_argument('-n', type=int, default=300)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--vars', type=int, default=60)
    ap.add_argument('--timeout', type=float, default=30)
    ap.add_argument('--keep', default=os.path.join(HERE, 'failures'))
    args = ap.parse_args()
    solvers = [s for s in SOLVERS if os.path.exists(s)]
    rng = random.Random(args.seed)
    os.makedirs(args.keep, exist_ok=True)
    tmp = os.path.join(args.keep, 'current.qdimacs')
    fails, counts = 0, {True: 0, False: 0, 'timeout': 0}
    for i in range(args.n):
        with open(tmp, 'w') as f:
            f.write(random_qdimacs(rng, args.vars))
        expected = run([args.depqbf, tmp], args.timeout)
        if expected == 'timeout':
            counts['timeout'] += 1
            continue
        counts[expected] = counts.get(expected, 0) + 1
        for s in solvers:
            got = run([s, tmp], args.timeout)
            if got != expected:
                fails += 1
                name = os.path.join(args.keep, f'depqbf_fail_{args.seed}_{i}.qdimacs')
                os.replace(tmp, name)
                print(f'[{i}] {os.path.relpath(s, HERE)}: DepQBF says {expected}, got {got} -> {name}')
                break
    if os.path.exists(tmp):
        os.remove(tmp)
    print(f'{args.n} formulas ({counts[True]} true, {counts[False]} false, '
          f'{counts["timeout"]} DepQBF timeouts), {len(solvers)} solver(s), {fails} failure(s)')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
