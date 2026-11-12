"""DQDIMACS reader/writer and a brute-force reference evaluator.

A formula is (domains, prefix, clauses):
  domains: dict var -> domain size
  prefix:  list of (qtype, [vars]) outermost first, qtype in {'e', 'a'}
  clauses: list of clauses, each a list of (var, frozenset_of_values)
"""
import sys


def parse_lit(tok, domains):
    neg = tok.startswith('-')
    if neg:
        tok = tok[1:]
    if ':' in tok:
        v, items = tok.split(':', 1)
        var = int(v)
        vals = set()
        for item in items.split(','):
            if '-' in item:
                a, b = item.split('-')
                vals.update(range(int(a), int(b) + 1))
            else:
                vals.add(int(item))
    else:
        var = int(tok)
        vals = {1}
    dom = domains[var]
    assert all(0 <= x < dom for x in vals), f"value out of domain: {tok}"
    if neg:
        vals = set(range(dom)) - vals
    return var, frozenset(vals)


def read(path):
    domains, prefix, clauses, cur = {}, [], [], []
    with open(path) as f:
        for line in f:
            toks = line.split()
            if not toks or toks[0] == 'c':
                continue
            if toks[0] == 'p':
                n = int(toks[2])
                domains = {x: 2 for x in range(1, n + 1)}
                continue
            if toks[0] == 'd':
                dom = int(toks[1])
                for t in toks[2:]:
                    if t == '0':
                        break
                    domains[int(t)] = dom
                continue
            if toks[0] in ('a', 'e'):
                vs = [int(t) for t in toks[1:] if t != '0']
                prefix.append((toks[0], vs))
                continue
            for t in toks:
                if t == '0':
                    clauses.append(cur)
                    cur = []
                else:
                    cur.append(parse_lit(t, domains))
    quantified = {x for _, vs in prefix for x in vs}
    free = [x for x in domains if x not in quantified]
    if free:
        prefix.insert(0, ('e', free))
    return domains, prefix, clauses


def fmt_set(s):
    return ','.join(str(v) for v in sorted(s))


def write(f, domains, prefix, clauses, comment=None):
    if comment:
        for line in comment.splitlines():
            f.write(f"c {line}\n")
    f.write(f"p dcnf {len(domains)} {len(clauses)}\n")
    by_dom = {}
    for x, d in sorted(domains.items()):
        if d != 2:
            by_dom.setdefault(d, []).append(x)
    for d, vs in sorted(by_dom.items()):
        f.write(f"d {d} {' '.join(map(str, vs))} 0\n")
    for q, vs in prefix:
        f.write(f"{q} {' '.join(map(str, vs))} 0\n")
    for c in clauses:
        f.write(' '.join(f"{x}:{fmt_set(s)}" for x, s in c) + " 0\n")


def evaluate(domains, prefix, clauses):
    """Brute-force QBF evaluation by expanding the prefix (outermost first)."""
    quantified = {x for _, vs in prefix for x in vs}
    free = [('e', x) for x in sorted(domains) if x not in quantified]
    order = free + [(q, x) for q, vs in prefix for x in vs]
    # Merge duplicate variables, drop empty literals and tautologies.
    norm = []
    for c in clauses:
        m = {}
        for x, s in c:
            m[x] = m.get(x, frozenset()) | s
        if any(len(s) == domains[x] for x, s in m.items()):
            continue
        norm.append(tuple((x, s) for x, s in m.items() if s))
    if any(len(c) == 0 for c in norm):
        return False

    def rec(i, cls):
        if not cls:
            return True
        q, x = order[i]
        results = []
        for v in range(domains[x]):
            new, dead = [], False
            for c in cls:
                if any(y == x and v in s for y, s in c):
                    continue
                r = tuple((y, s) for y, s in c if y != x)
                if not r:
                    dead = True
                    break
                new.append(r)
            val = (not dead) and rec(i + 1, new)
            if q == 'e' and val:
                return True
            if q == 'a' and not val:
                return False
        return q == 'a'

    return rec(0, norm)


if __name__ == '__main__':
    r = evaluate(*read(sys.argv[1]))
    print("s cnf", 1 if r else 0)
    sys.exit(10 if r else 20)
