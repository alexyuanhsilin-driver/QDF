# DiscQBF: QCDCL for QBFs over discrete variables

New here? Start with [INTRODUCTION.md](INTRODUCTION.md), which explains the ideas.

DSAT's discrete literals carried over to DepQBF-style QBF solving. There are
two implementations of the same algorithm:

- `c/`: C99, in the style of DepQBF (`solver.c`, `solver.h`, `bits.h`, `main.c`)
- `cpp/`: C++20, easier to read (`Solver.cpp`, `Solver.hpp`, `Bits.hpp`, `main.cpp`)

They match function for function and use the same heuristics in the same
order. On every input they make the same decisions and print the same
statistics, and the fuzzer checks this.

```
make            # builds c/discqbf and cpp/discqbf
make debug      # *-debug builds with assertions + ASan/UBSan
make test       # full test suite (tests/run_tests.sh)
c/discqbf -v tests/cases/07_cycle5_coloring_forall_first.dqdimacs
```

Exit code 10 means true and 20 means false, as in DepQBF. Output is `s cnf 1|0 <vars> <clauses>`.
Options: `-v` prints statistics, `-v -v` also prints learned constraints and `-v -v -v` also prints decisions.
`--restart-base=N` and `--max-learned=N` are mainly for testing.

## Semantics

Each variable `x` has a domain `{0, ..., k-1}`. A literal `x ∈ S` is true when
the value of `x` lies in `S`. Clauses are disjunctions of literals. `∀x`
ranges over all `k` values and `∃x` over at least one. With every `k = 2`,
this is exactly ordinary QBF (`v` is `v:1`, `-v` is `v:0`).

## Input format (DQDIMACS)

The format is a superset of QDIMACS, so every `.qdimacs` file is accepted.

```
c comment
p dcnf 3 2          <- "p cnf" is accepted as well
d 3 1 2 0           <- domain size 3 for variables 1 and 2 (default size: 2)
d 200 3 0
a 1 0               <- quantifier blocks, outermost first
e 2 3 0
1:0,2 -2:1 0        <- (x1 ∈ {0,2}) ∨ (x2 ∉ {1})
3:5-9,100 2 0       <- (x3 ∈ {5..9, 100}) ∨ (x2 ∈ {1})
```

- A literal is written `v`, `-v`, `v:SET` or `-v:SET`, where `SET` is a comma list of
  values or ranges `a-b`. A leading `-` complements the set, and a bare `v` means `v:1`.
- Literals on the same variable in one clause are merged, since `x∈A ∨ x∈B` is `x∈A∪B`.
  A clause whose set covers the whole domain is a tautology and is dropped.
- Variables not in any block are free and become outermost existentials, as in DepQBF.
- Domain and prefix lines must come before the clauses.

## Algorithm and its relation to DepQBF

This is DepQBF's QCDCL with `--dep-man=simple`, i.e. the linear quantifier
prefix with no dependency scheme. Without a dependency scheme there is no
need for `qdpll_dep_man_qdag.c` (DepQBF implements "simple" mode inside that file too).

| DepQBF (`qdpll.c`) | here |
|---|---|
| assignments TRUE/FALSE | `cur(x)`: set of values still possible. A decision fixes `x = v`. Propagation removes values, and each removed value records its trail event (as in DSAT) |
| clauses + learned cubes in one framework | the same: each literal stores a set `B` (`S` for clauses, `dom∖S` for cubes), so one code path does CDCL and SDCL |
| unit / empty constraint incl. universal (existential) reduction | `evaluate()`: literal is DEAD if `cur∩B=∅`, FULL if `cur⊆B`, OPEN otherwise. Unit narrows `cur := cur∩B`. An open other-type literal is reducible only on an *untouched* variable (rule 1 below) |
| `choose_var`, `peek_tautology` | `wPivot` / `w_pivot`: latest propagation event on an own-type literal, with the same fallback when the resolvent would be a tautology |
| `resolve_and_reduce` | discrete Q-resolution: the pivot sets are intersected, all other sets are unioned, then reduction is applied |
| `stop_resolution` | `wStop` / `w_stop`: same three conditions, plus one extra check (rule 2 below) |
| initial cube from a satisfying assignment | `initialCube` / `initial_cube`. For variable `x`, the cube literal is `x ∈ ⋂ S` over the clauses that `x` satisfies |
| VSIDS-like activities, restarts, learned-constraint deletion | per-variable activity inside the leftmost open block, Luby restarts, the oldest half of the non-reason learned constraints is deleted |

### Two rules that discrete domains need

In Boolean QBF a variable is either unassigned or fixed. With discrete domains
there is a third state: **touched**, meaning some values have been removed
but more than one is left. For example, a learned cube can force a universal
`x ∈ {0,1,2}` to avoid 0. The "other type" below means universal for clauses
and existential for cubes.

1. **Propagation (`evaluate`).** An open other-type literal counts as
   reducible only if its variable is untouched. On a touched variable it
   blocks the constraint until the variable is narrowed further. Without this
   rule, open pieces like `x∈{1}` and `x∈{2}` from different clauses merge
   during learning into `x∈{1,2}`. Resolving that with a clause containing
   `x∈{0}` gives a tautology, and learning gets stuck with "no pivot found"
   (`tests/cases/17_*`). Random search found 3 such formulas in 90 million
   before this rule and none after it.
2. **Stop criterion (`wStop`).** Every other-type literal right of the
   asserting literal must be false, or on an untouched variable, at the
   backjump level. Otherwise rule 1 would not let the learned constraint act
   as unit there (`tests/cases/18_*`). This check never fires on Boolean
   formulas.

### Left out on purpose (from DepQBF 6.03)

These were left out: dependency schemes (qdag), generalized axioms
(PicoSAT/Nenofex oracles), blocked clause elimination (QBCE), long-distance
resolution, the incremental/clause-group API, assumptions, traces and
certificates, and pure literals.

### Known limitations

- Propagation visits every constraint containing a changed variable. There are no
  two-watched literals yet, which is the main performance gap to DepQBF.
- Like DepQBF without long-distance resolution, learning stops with "no pivot
  found" (exit code 2) if traditional Q-resolution gets stuck. After the two
  rules above, this did not happen in 90 million random formulas. That is
  evidence, not a proof.

## Tests (`tests/`)

- `cases/*.dqdimacs`: hand-written formulas with `c expect: true|false`, run by `run_cases.py`.
- `dqdimacs.py`: format reader/writer and a brute-force expansion solver used as the reference.
- `fuzz.py`: random discrete QBFs, checked against brute force. It also requires the C and C++ builds to
  report identical statistics. `--stress` restarts after every conflict and keeps
  4 learned constraints. `--no-reference` is for formulas too large to brute-force.
- `vs_depqbf.py DEPQBF`: random Boolean QBFs (up to hundreds of variables) compared with the original DepQBF.
- `encode.py`: translates a discrete QBF into a Boolean QDIMACS formula (binary encoding, see the file header).
- `vs_depqbf_encoded.py DEPQBF`: random discrete QBFs, DiscQBF on the formula vs. DepQBF on its Boolean encoding.
  `--reference` adds brute force; `--cases` runs the hand-written cases.
- `run_tests.sh [DEPQBF]`: all of the above.
