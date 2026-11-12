"""Encodes a discrete QBF as a Boolean QBF (QDIMACS), for checking DiscQBF
against Boolean QBF solvers such as DepQBF.

usage: python3 encode.py in.dqdimacs > out.qdimacs

Encoding (log/binary), for a variable x with domain k and nb = ceil(log2 k) bits:

  bits      b[x,0..nb-1], quantified like x, in x's block. "code(x) = v" means
            the bits spell v in binary.
  literal   x in S  ->  OR_{v in S} t[x,v], where t[x,v] is an innermost
            existential with  t[x,v] -> code(x) = v  (one clause per bit).
            With nb = 1 the bit is used directly (v=1: b, v=0: -b).
  invalid   codes k..2^nb-1:
            x existential: forbidden by one clause per invalid code.
            x universal:   the universal player must not win by choosing an
                           invalid code. An innermost existential inv[x] with
                           inv[x] -> code(x) is invalid is added to every
                           original clause, so an invalid code satisfies the
                           whole matrix.

Innermost auxiliary existentials that are only constrained by implications
"aux -> condition on outer variables" do not change the truth value: the
existential player can set each one to exactly its condition.
"""
import sys

import dqdimacs


def encode(domains, prefix, clauses):
    quantified = {x for _, vs in prefix for x in vs}
    free = [x for x in sorted(domains) if x not in quantified]
    blocks = ([('e', free)] if free else []) + list(prefix)
    qtype = {x: q for q, vs in blocks for x in vs}

    next_id = [0]

    def new():
        next_id[0] += 1
        return next_id[0]

    nbits = {x: (domains[x] - 1).bit_length() for x in domains}
    bits = {x: [new() for _ in range(nbits[x])] for x in sorted(domains)}

    out_clauses = []
    aux = []

    def code_not(x, v):
        """Clause literals stating code(x) != v."""
        return [b if not (v >> i) & 1 else -b for i, b in enumerate(bits[x])]

    def code_eq_lits(x, v):
        """Unit conditions whose conjunction states code(x) = v."""
        return [b if (v >> i) & 1 else -b for i, b in enumerate(bits[x])]

    t = {}

    def lit_var(x, v):
        if nbits[x] == 1:
            return bits[x][0] if v == 1 else -bits[x][0]
        if (x, v) not in t:
            tv = new()
            t[(x, v)] = tv
            aux.append(tv)
            for lit in code_eq_lits(x, v):
                out_clauses.append([-tv, lit])
        return t[(x, v)]

    inv = []
    for x in sorted(domains):
        k, nb = domains[x], nbits[x]
        invalid = range(k, 1 << nb)
        if not invalid:
            continue
        if qtype[x] == 'e':
            for c in invalid:
                out_clauses.append(code_not(x, c))
        else:
            iv = new()
            aux.append(iv)
            inv.append(iv)
            for v in range(k):
                out_clauses.append([-iv] + code_not(x, v))

    for c in clauses:
        merged = {}
        for x, s in c:
            merged[x] = merged.get(x, frozenset()) | s
        if any(len(s) == domains[x] for x, s in merged.items()):
            continue  # tautology
        lits = [lit_var(x, v) for x, s in sorted(merged.items()) for v in sorted(s)]
        out_clauses.append(lits + inv)

    qblocks = []
    for q, vs in blocks:
        bs = [b for x in vs for b in bits[x]]
        if not bs:
            continue
        if qblocks and qblocks[-1][0] == q:
            qblocks[-1][1].extend(bs)
        else:
            qblocks.append((q, bs))
    if aux:
        if qblocks and qblocks[-1][0] == 'e':
            qblocks[-1][1].extend(aux)
        else:
            qblocks.append(('e', aux))

    lines = [f'p cnf {next_id[0]} {len(out_clauses)}']
    lines += [f"{q} {' '.join(map(str, vs))} 0" for q, vs in qblocks]
    lines += [' '.join(map(str, c)) + ' 0' for c in out_clauses]
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    sys.stdout.write(encode(*dqdimacs.read(sys.argv[1])))
