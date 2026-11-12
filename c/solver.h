/* DiscQBF: a QCDCL solver for QBFs over discrete (multi-valued) variables.

   The search follows DepQBF's QCDCL with the "simple" dependency manager:
   decisions respect the linear quantifier prefix, conflicts produce learned
   clauses and solutions produce learned cubes via traditional Q-resolution
   (no long-distance resolution, no dependency schemes, no generalized axioms).

   Literals are DSAT-style: a literal is a pair (x, S) where S is a set of
   values of variable x.

   Unified representation (as in DepQBF, clauses and cubes share one engine):
   every constraint literal stores a value set B, interpreted as follows.
     clause literal x in S  ->  B = S
     cube   literal x in S  ->  B = dom(x) \ S
   With cur(x) being the values x may still take, a literal is
     DEAD  if cur(x) & B == {}   (clause literal false / cube literal true)
     FULL  if cur(x) is a subset of B (clause literal true / cube literal false)
     OPEN  otherwise.
   A constraint's "own" quantifier type is EXISTS for clauses and FORALL for
   cubes. Unit propagation of an open own-type literal (x, B) always narrows
   cur(x) to cur(x) & B; resolution on a pivot always intersects the pivot
   sets and unions all other sets. This lets one piece of code do both CDCL
   (clauses) and SDCL (cubes).

   This file is a line-by-line twin of ../cpp/Solver.hpp. */
#ifndef DISCQBF_SOLVER_H
#define DISCQBF_SOLVER_H

#include "bits.h"

typedef enum { QTYPE_EXISTS = 0, QTYPE_FORALL = 1 } QType;
typedef enum { RESULT_UNKNOWN = 0, RESULT_TRUE = 10, RESULT_FALSE = 20 } Result;

typedef struct Solver Solver;

typedef struct
{
  long long decisions, propagations, conflicts, solutions;
  long long restarts, deleted, backjumps;
} Stats;

Solver *solver_new (void);
void solver_delete (Solver * s);

/* Formula construction. Variables are numbered 1..n. Call set_num_vars
   first, then set_domain, add_block (outermost first) and add_clause.
   The first three return an error message or NULL. */
void solver_set_num_vars (Solver * s, int n);
const char *solver_set_domain (Solver * s, int var, int domain);
const char *solver_add_block (Solver * s, QType q, const int *vars, int n);
/* Clause literal i is (vars[i] in sets[i]); duplicate variables are merged. */
void solver_add_clause (Solver * s, const int *vars, word * const *sets, int n);
int solver_num_vars (const Solver * s);
int solver_domain (const Solver * s, int var);

/* Returns RESULT_TRUE or RESULT_FALSE; aborts the program on internal errors. */
Result solver_solve (Solver * s);

void solver_set_verbosity (Solver * s, int v);
/* restart_base: conflicts (+ solutions) per Luby restart unit (default 100);
   max_learned: learned constraints kept before the first reduction (default 2000). */
void solver_set_limits (Solver * s, int restart_base, int max_learned);
const Stats *solver_stats (const Solver * s);

#endif
