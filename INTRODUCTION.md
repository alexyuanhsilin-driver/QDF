# DiscQBF: an introduction

DiscQBF decides quantified Boolean formulas whose variables are **discrete**:
each variable ranges over a finite domain `{0, …, k−1}` instead of
`{false, true}`. It combines two existing solvers:

- **DSAT** (Zhang, Zhou, Darwiche, SAT 2026) supplies the literals. A literal
  says "x takes one of these values", and the solver reasons on value sets
  directly instead of encoding them into Booleans.
- **DepQBF** (Lonsing, Egly) supplies the search: QCDCL with clause learning
  from conflicts and cube learning from solutions, using traditional
  Q-resolution.

Dependency schemes are left out. Decisions follow the quantifier prefix,
as in DepQBF's `--dep-man=simple`. There are two implementations of the same
algorithm, `c/` (C99) and `cpp/` (C++20). They make identical decisions and
report identical statistics.

This document explains the ideas. [`README.md`](README.md) is the reference
for building, the input format and the code layout.

---

## 1. Discrete QBF

**Variables and domains.** Variable `x` has domain `dom(x) = {0, …, k−1}`.
With every `k = 2` this is ordinary QBF.

**Literals.** `x ∈ S` with `S ⊆ dom(x)`. It is true when the value of `x`
lies in `S`.

```
x ∈ {0,2}        "x is 0 or 2"
x ∉ {1}  =  x ∈ dom(x) ∖ {1}
```

**Formulas.** A quantifier prefix (outermost first) and a CNF matrix.

```
∀x ∃y.  (x ∈ {0} ∨ y ∈ {1}) ∧ (x ∈ {1,2} ∨ y ∈ {0})        x, y ∈ {0,1,2}
```

`∀x` means for every value of `dom(x)`, and `∃y` means for some value. The
example is true: the ∃ player plays `y = 1` if `x = 0` and `y = 0` otherwise.

**Two facts used everywhere:**

```
x∈A ∨ x∈B   =  x ∈ A∪B           (literals on one variable merge)
x∈dom(x)    =  true              (tautology)
```

## 2. Input format

DQDIMACS is QDIMACS plus domain lines, so any QDIMACS file is valid input.

```
p dcnf 2 2
d 3 1 2 0          domain size 3 for variables 1 and 2 (default 2)
a 1 0              ∀x1
e 2 0              ∃x2
1:0 2:1 0          x1∈{0} ∨ x2∈{1}
1:1,2 2:0 0        x1∈{1,2} ∨ x2∈{0}
```

Literal syntax: `v:SET`, `-v:SET` (complement), `v` = `v:1`, `-v` = `-v:1`,
where `SET` is a comma list of values or ranges `a-b`.

```
c/discqbf file.dqdimacs        exit 10 = true, 20 = false
cpp/discqbf -v -v -v file      trace decisions and learned constraints
```

## 3. How the solver works

### 3.1 State

Instead of a TRUE/FALSE assignment, every variable has a set of **remaining
values**:

```
cur(x) ⊆ dom(x)          starts as dom(x), only shrinks along a branch
```

Each removed value is recorded on the trail with its decision level and its
reason: a decision, a clause, or a cube.

### 3.2 Literal states

For a clause literal `x ∈ S`:

| state | condition | meaning |
|---|---|---|
| false | `cur(x) ∩ S = ∅` | no remaining value is in S |
| true | `cur(x) ⊆ S` | every remaining value is in S |
| open | otherwise | some remaining value makes it true, another false |

A variable is **untouched** if `cur(x) = dom(x)` and **fixed** if
`|cur(x)| = 1`. In Boolean QBF every variable is one of the two. With
discrete domains there is a third state, **touched**: some values removed,
more than one left. Section 4 is about this state.

### 3.3 The search loop

```
loop:
    propagate
        clause conflict   → learn a clause, backjump
        cube satisfied    → learn a cube, backjump
    all clauses true      → build a cube from the assignment, learn, backjump
    otherwise             → decide: x := v
                            x from the outermost block with an unfixed variable
learning derives the empty clause → FALSE
learning derives the empty cube   → TRUE
```

### 3.4 Propagation

For a clause C, ignoring false literals:

```
some literal true                          → satisfied
no open ∃ literal                          → CONFLICT
exactly one open ∃ literal  x∈S,
  every open ∀ literal right of x          → UNIT:  cur(x) := cur(x) ∩ S
```

Open ∀ literals right of the unit literal are ignored by **universal
reduction**: the ∀ player can make them false. Unlike Boolean unit
propagation, a unit clause usually **narrows** `x` instead of fixing it.
That is DSAT's contribution.

### 3.5 Learning: Q-resolution on value sets

Resolution on the pivot `x`:

```
x∈S ∨ α
x∈T ∨ β
─────────────────────
x∈S∩T ∨ α ∨ β           (literals on other variables merge by ∪)
```

If `S∩T = ∅`, the pivot literal disappears. A resolvent is **forbidden** if
a ∀ variable merges to its whole domain (a tautology). After every step,
**universal reduction** removes ∀ literals that have no ∃ literal to their
right.

The working clause W starts as the conflict clause. The solver resolves
backwards along the trail with the **reasons** of propagated ∃ values (a
decision has no reason clause) until W satisfies the **stop criterion**:

```
1. exactly one ∃ literal A at the highest level dmax
2. dmax > 0
3. the decision at dmax was ∃
4. ∀ literals left of A are false below dmax
5. bt = deepest level of the other literals          (backjump level)
6. ∀ literals right of A: false or untouched at bt    (rule 2, Section 4)
```

The solver then backjumps to `bt`, where W is **unit** and narrows A's
variable.

### 3.6 Cubes: the same engine, mirrored

A cube is a conjunction of literals. A learned cube means "if the ∀ player
satisfies this, the ∃ player wins". Everything above holds with ∃ and ∀
swapped. Internally a cube literal `x ∈ S` is stored as its complement
`dom(x) ∖ S`, so one code path handles both clauses and cubes.

## 4. What discrete domains change

A cube can force a ∀ variable to **avoid** a value. That can leave the
variable touched:

```
cube [x3∈{0}] unit  →  cur(x3) := {0,1,2} ∖ {0} = {1,2}
```

A touched variable can be treated as **fixed** by one clause and as **free**
by another. When learning combines those clauses, it can get stuck.

**Example** (`tests/cases/17_partly_narrowed_universal.dqdimacs`, FALSE):

```
∃x1 ∀x3 ∃x2      x3 ∈ {0,1,2}

C1 = x3∈{0} ∨ x2∈{1}
C2 = x1∈{1} ∨ x3∈{1} ∨ x2∈{0}
C3 = x1∈{0} ∨ x3∈{2} ∨ x2∈{0}
C4 = x3∈{2} ∨ x2∈{0}
```

After the cube, at level 0 with `cur(x3) = {1,2}`, the original
DepQBF-style rules do this:

```
C1: x3∈{0} false               (fixed view: uses "x3 ≠ 0")     → x2 := 1
C2: x3∈{1} reduced             (free view: ∀ plays x3 = 2)     → x1 := 1
C3: x3∈{2} reduced             (free view: ∀ plays x3 = 1)     → conflict

learning:  C3 ⊗ C2 on x1   → x3∈{1,2} ∨ x2∈{0}
           ⊗ C1 on x2      → x3∈{0,1,2}   tautology, no other pivot → stuck
```

The merged literal `x3∈{1,2}` is false only if `x3 = 0`, which the cube
banned. In Boolean QBF this cannot happen, because a variable is either free
or fixed.

**Rule 1 (propagation).** An open ∀ literal may be reduced only if its
variable is untouched. On a touched variable it **blocks** the clause until
the variable is decided. Mirrored for cubes: an open ∃ literal may be
∃-reduced only if its variable is untouched.

**Rule 2 (stop criterion, condition 6).** Every ∀ literal right of A must be
false, or on an untouched variable, at the backjump level. Otherwise rule 1
would keep the learned clause from being unit there.

With the rules, the example runs like this instead:

```
L0  C2, C3, C4 blocked (x3 touched)
L1  decide x1 := 0
L2  decide x3 := 1                  → C4 conflict
learning:  C4 ⊗ C1 on x2  →  x3∈{0,2}   →  ∀-reduce  →  ∅   FALSE ✓
```

Rule 1 is **sufficient, not minimal**: it also blocks reductions that would
have been harmless, because propagation looks at one clause at a time and
cannot know what learning will combine later. On Boolean formulas neither
rule ever changes anything.

## 5. Relation to DepQBF and DSAT

| | DepQBF 6.03 | DSAT | DiscQBF |
|---|---|---|---|
| variables | Boolean | discrete | discrete |
| quantifiers | yes | no (SAT) | yes |
| literal | `x`, `¬x` | `x ∈ S` | `x ∈ S` |
| decision | `x := v` | `x ∈ B` (prunes the other values) | `x := v` |
| learning | clauses + cubes, Q-resolution | clauses | clauses + cubes, Q-resolution on sets |
| dependency schemes | standard, rrs | – | none (prefix order) |
| generalized axioms, QBCE, LDQ, incremental API | yes | – | no |
| propagation | watched literals | literals watch one of their states | occurrence lists (simpler, slower) |

## 6. How the results were checked

- **Brute force:** about 33,000 random small discrete formulas compared with
  a separate expansion-based evaluator (`tests/dqdimacs.py`), including runs
  with sanitizers and stress settings (restart after every conflict, 4
  learned constraints kept).
- **C vs C++:** identical answers and identical statistics on every formula.
- **Crash search:** 90 million tiny formulas with no internal errors after
  rules 1 and 2. Before them, 3 formulas got stuck.
- **DepQBF on Boolean formulas:** about 1,500 random QBFs with up to 300
  variables.
- **DepQBF on discrete formulas, through a Boolean encoding**
  (`tests/encode.py`, `tests/vs_depqbf_encoded.py`). Each discrete variable
  becomes ⌈log₂ k⌉ bits. Literals become innermost auxiliary existentials
  `t → (bits = v)`. Invalid bit patterns are forbidden for existentials and
  make every clause true for universals. DiscQBF on the original was compared
  with DepQBF on the encoding:

  | formulas | size | result |
  |---|---|---|
  | 20 hand-written cases | – | all agree (and with brute force) |
  | 3,000 random | ≤ 8 variables, domains ≤ 6 | all agree, also with brute force |
  | 2,000 random | 30–200 variables, domains 3–70 | all agree |
  | 259 structured 2/3-level | up to 175 variables, up to 1,508 learned constraints | all agree (1 DepQBF timeout) |

  The encoding itself was first checked against brute force on 2,835 tiny
  formulas.

Limits: every reference (brute force, encoding) was written by the same
author as the solver, and DepQBF only sees the encoded formula. The solver
does not produce certificates (Q-resolution proofs or Skolem functions).
