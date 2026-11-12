/* Twin of ../cpp/Solver.cpp: same algorithm, same heuristics, same order of
   operations, so both versions make identical decisions and report identical
   statistics. Comments explaining the algorithm are kept in sync. */
#include "solver.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- */
/* Dynamic arrays                                                            */
/* ------------------------------------------------------------------------- */

static void *
xrealloc (void *p, size_t n)
{
  p = realloc (p, n ? n : 1);
  if (!p)
    {
      fprintf (stderr, "out of memory\n");
      exit (2);
    }
  return p;
}

#define VEC(T) struct { T *a; int n, cap; }
#define VEC_PUSH(v, x)                                                  \
  do {                                                                  \
    if ((v).n == (v).cap)                                               \
      {                                                                 \
        (v).cap = (v).cap ? 2 * (v).cap : 8;                            \
        (v).a = xrealloc ((v).a, (v).cap * sizeof (*(v).a));            \
      }                                                                 \
    (v).a[(v).n++] = (x);                                               \
  } while (0)
#define VEC_RESERVE(v, k)                                               \
  do {                                                                  \
    while ((v).cap < (k))                                               \
      {                                                                 \
        (v).cap = (v).cap ? 2 * (v).cap : 8;                            \
        (v).a = xrealloc ((v).a, (v).cap * sizeof (*(v).a));            \
      }                                                                 \
  } while (0)

typedef VEC (int) IntVec;
typedef VEC (word) WordVec;

/* ------------------------------------------------------------------------- */
/* Data structures                                                           */
/* ------------------------------------------------------------------------- */

typedef struct
{
  int var;
  word *set;                    /* the stored set B (see solver.h) */
} Lit;

typedef struct
{
  Lit *lits;                    /* one literal per variable, sorted by (block, var) */
  int nlits;
  word *words;                  /* memory of all literal sets */
  char is_cube, learned, deleted;
  int reason_refs;              /* number of trail events using this as reason */
} Constraint;

typedef struct
{
  int domain, nw;
  QType qtype;
  int block;                    /* index of quantifier block, 0 = outermost */
  word *cur;                    /* values still possible */
  int *rm;                      /* rm[v] = trail index of event that removed v, -1 if present */
  double activity;
  int saved;                    /* phase saving: last value the variable was fixed to */
  IntVec occs;                  /* constraints (ids) containing this variable */
} Var;

/* One entry on the trail: values removed from a variable's domain. */
typedef struct
{
  int var;
  int removed;                  /* offset of the removed set in 'trail_words' */
  int reason;                   /* constraint id, or -1 for a decision */
  int level;
} Event;

typedef VEC (Event) EventVec;
typedef VEC (Constraint) ConsVec;
typedef VEC (QType) QTypeVec;
typedef VEC (IntVec) IntVecVec;

struct Solver
{
  int nvars;
  Var *vars;                    /* index 0 unused */
  QTypeVec block_qtype;
  IntVecVec block_vars;
  int prefix_done, empty_clause_seen;

  ConsVec cons;
  IntVec original_clauses;
  int num_learned, max_learned;
  int restart_base;

  EventVec trail;
  WordVec trail_words;
  int qhead;                    /* next trail event to propagate */
  QTypeVec dec_qtype;           /* dec_qtype[l] = qtype of decision at level l (l >= 1) */
  IntVec level_start;
  int level;

  double var_inc;
  long long luby_index, conflicts_until_restart;

  /* Working constraint for learning, indexed by variable. */
  word **wset;
  char *in_w;
  IntVec wvars;

  word *tmp;                    /* scratch set, large enough for any variable */
  int max_nw;

  Stats stats;
  int verbosity;
};

typedef enum { EV_NONE, EV_SATISFIED, EV_UNIT, EV_EMPTY } EvalKind;

typedef struct
{
  EvalKind kind;
  int lit;                      /* index of the unit literal */
} Eval;

static void
internal_error (const char *msg)
{
  fprintf (stderr, "internal error: %s\n", msg);
  exit (2);
}

static word *
event_removed (Solver * s, const Event * e)
{
  return s->trail_words.a + e->removed;
}

/* ------------------------------------------------------------------------- */
/* Formula construction                                                      */
/* ------------------------------------------------------------------------- */

Solver *
solver_new (void)
{
  Solver *s = xrealloc (0, sizeof (Solver));
  memset (s, 0, sizeof (Solver));
  s->max_learned = 2000;
  s->restart_base = 100;
  s->var_inc = 1.0;
  return s;
}

void
solver_delete (Solver * s)
{
  int i;
  for (i = 0; s->vars && i <= s->nvars; i++)
    {
      free (s->vars[i].cur);
      free (s->vars[i].rm);
      free (s->vars[i].occs.a);
      if (s->wset)
        free (s->wset[i]);
    }
  for (i = 0; i < s->cons.n; i++)
    {
      free (s->cons.a[i].lits);
      free (s->cons.a[i].words);
    }
  for (i = 0; i < s->block_vars.n; i++)
    free (s->block_vars.a[i].a);
  free (s->vars);
  free (s->block_qtype.a);
  free (s->block_vars.a);
  free (s->cons.a);
  free (s->original_clauses.a);
  free (s->trail.a);
  free (s->trail_words.a);
  free (s->dec_qtype.a);
  free (s->level_start.a);
  free (s->wset);
  free (s->in_w);
  free (s->wvars.a);
  free (s->tmp);
  free (s);
}

void
solver_set_num_vars (Solver * s, int n)
{
  int x;
  s->nvars = n;
  s->vars = xrealloc (0, (n + 1) * sizeof (Var));
  memset (s->vars, 0, (n + 1) * sizeof (Var));
  for (x = 0; x <= n; x++)
    {
      s->vars[x].domain = 2;
      s->vars[x].block = -1;    /* -1 = not quantified yet */
      s->vars[x].saved = -1;
    }
}

int
solver_num_vars (const Solver * s)
{
  return s->nvars;
}

int
solver_domain (const Solver * s, int var)
{
  return s->vars[var].domain;
}

void
solver_set_verbosity (Solver * s, int v)
{
  s->verbosity = v;
}

void
solver_set_limits (Solver * s, int restart_base, int max_learned)
{
  s->restart_base = restart_base > 0 ? restart_base : 1;
  s->max_learned = max_learned > 0 ? max_learned : 1;
}

const Stats *
solver_stats (const Solver * s)
{
  return &s->stats;
}

const char *
solver_set_domain (Solver * s, int var, int domain)
{
  if (var < 1 || var > s->nvars)
    return "variable out of range";
  if (domain < 1)
    return "domain size must be positive";
  if (s->prefix_done)
    return "domain sizes must be given before the clauses";
  s->vars[var].domain = domain;
  return 0;
}

const char *
solver_add_block (Solver * s, QType q, const int *vs, int n)
{
  int i, b;
  if (s->prefix_done)
    return "quantifier blocks must be given before the clauses";
  if (!s->block_qtype.n || s->block_qtype.a[s->block_qtype.n - 1] != q)
    {
      IntVec empty = { 0, 0, 0 };
      VEC_PUSH (s->block_qtype, q);
      VEC_PUSH (s->block_vars, empty);
    }
  b = s->block_qtype.n - 1;
  for (i = 0; i < n; i++)
    {
      int x = vs[i];
      if (x < 1 || x > s->nvars)
        return "variable out of range";
      if (s->vars[x].block != -1)
        return "variable quantified twice";
      s->vars[x].block = b;
      s->vars[x].qtype = q;
      VEC_PUSH (s->block_vars.a[b], x);
    }
  return 0;
}

static int
cmp_int (const void *a, const void *b)
{
  return *(const int *) a - *(const int *) b;
}

/* Unquantified (free) variables go to an outermost existential block, as in DepQBF. */
static void
finish_prefix (Solver * s)
{
  int x, i, has_free = 0;
  if (s->prefix_done)
    return;
  s->prefix_done = 1;
  for (x = 1; x <= s->nvars; x++)
    if (s->vars[x].block == -1)
      has_free = 1;
  if (has_free)
    {
      if (!s->block_qtype.n || s->block_qtype.a[0] != QTYPE_EXISTS)
        {
          IntVec empty = { 0, 0, 0 };
          VEC_PUSH (s->block_qtype, QTYPE_EXISTS);
          VEC_PUSH (s->block_vars, empty);
          for (i = s->block_qtype.n - 1; i > 0; i--)
            {
              s->block_qtype.a[i] = s->block_qtype.a[i - 1];
              s->block_vars.a[i] = s->block_vars.a[i - 1];
            }
          s->block_qtype.a[0] = QTYPE_EXISTS;
          s->block_vars.a[0] = empty;
          for (x = 1; x <= s->nvars; x++)
            if (s->vars[x].block != -1)
              s->vars[x].block++;
        }
      for (x = 1; x <= s->nvars; x++)
        if (s->vars[x].block == -1)
          {
            s->vars[x].block = 0;
            s->vars[x].qtype = QTYPE_EXISTS;
            VEC_PUSH (s->block_vars.a[0], x);
          }
    }
  for (i = 0; i < s->block_vars.n; i++)
    qsort (s->block_vars.a[i].a, s->block_vars.a[i].n, sizeof (int), cmp_int);
  s->max_nw = 1;
  for (x = 1; x <= s->nvars; x++)
    {
      Var *v = s->vars + x;
      v->nw = bits_words (v->domain);
      if (v->nw > s->max_nw)
        s->max_nw = v->nw;
      v->cur = xrealloc (0, v->nw * sizeof (word));
      bits_fill (v->cur, v->domain);
      v->rm = xrealloc (0, v->domain * sizeof (int));
      for (i = 0; i < v->domain; i++)
        v->rm[i] = -1;
    }
  s->tmp = xrealloc (0, s->max_nw * sizeof (word));
}

static int
lit_before (Solver * s, const Lit * a, const Lit * b)
{
  if (s->vars[a->var].block != s->vars[b->var].block)
    return s->vars[a->var].block < s->vars[b->var].block;
  return a->var < b->var;
}

/* Copies the literals (vars[i], sets[i]) into a new constraint. */
static int
add_constraint (Solver * s, const int *vs, word * const *sets, int n, int is_cube, int learned)
{
  Constraint c;
  int i, j, total = 0, id = s->cons.n;
  for (i = 0; i < n; i++)
    total += s->vars[vs[i]].nw;
  c.lits = xrealloc (0, n * sizeof (Lit));
  c.words = xrealloc (0, total * sizeof (word));
  c.nlits = n;
  c.is_cube = is_cube;
  c.learned = learned;
  c.deleted = 0;
  c.reason_refs = 0;
  total = 0;
  for (i = 0; i < n; i++)
    {
      int nw = s->vars[vs[i]].nw;
      c.lits[i].var = vs[i];
      c.lits[i].set = c.words + total;
      bits_copy (c.lits[i].set, sets[i], nw);
      total += nw;
    }
  /* insertion sort by (block, var) */
  for (i = 1; i < n; i++)
    {
      Lit l = c.lits[i];
      for (j = i; j > 0 && lit_before (s, &l, &c.lits[j - 1]); j--)
        c.lits[j] = c.lits[j - 1];
      c.lits[j] = l;
    }
  for (i = 0; i < n; i++)
    VEC_PUSH (s->vars[c.lits[i].var].occs, id);
  VEC_PUSH (s->cons, c);
  if (learned)
    s->num_learned++;
  return id;
}

void
solver_add_clause (Solver * s, const int *in_vars, word * const *in_sets, int n)
{
  int *lv = xrealloc (0, (n + 1) * sizeof (int));
  word **ls = xrealloc (0, (n + 1) * sizeof (word *));
  int i, j, m = 0, maxb = -1, k = 0;
  finish_prefix (s);
  /* Merge literals on the same variable: (x in S1) or (x in S2) = x in S1|S2. */
  for (i = 0; i < n; i++)
    {
      int x = in_vars[i], merged = 0, nw = s->vars[x].nw;
      if (bits_empty (in_sets[i], nw))
        continue;               /* x in {} is false */
      for (j = 0; j < m; j++)
        if (lv[j] == x)
          {
            bits_or (ls[j], in_sets[i], nw);
            merged = 1;
          }
      if (!merged)
        {
          lv[m] = x;
          ls[m] = xrealloc (0, nw * sizeof (word));
          bits_copy (ls[m], in_sets[i], nw);
          m++;
        }
    }
  for (i = 0; i < m; i++)
    if (bits_is_full (ls[i], s->vars[lv[i]].domain))
      goto DONE;                /* tautology */

  /* Universal reduction: drop universal literals right of all existential ones. */
  for (i = 0; i < m; i++)
    if (s->vars[lv[i]].qtype == QTYPE_EXISTS && s->vars[lv[i]].block > maxb)
      maxb = s->vars[lv[i]].block;
  for (i = 0; i < m; i++)
    if (s->vars[lv[i]].qtype == QTYPE_EXISTS || s->vars[lv[i]].block < maxb)
      {
        word *t = ls[k];
        int tv = lv[k];
        ls[k] = ls[i];
        lv[k] = lv[i];
        ls[i] = t;
        lv[i] = tv;
        k++;
      }

  if (!k)
    s->empty_clause_seen = 1;
  else
    VEC_PUSH (s->original_clauses, add_constraint (s, lv, ls, k, 0, 0));
DONE:
  for (i = 0; i < m; i++)
    free (ls[i]);
  free (lv);
  free (ls);
}

/* ------------------------------------------------------------------------- */
/* Propagation                                                               */
/* ------------------------------------------------------------------------- */

/* Classifies a constraint under the current domains (see solver.h).
   EMPTY means: no FULL literal and no OPEN own-type literal; for a clause that
   is a conflict, for a cube a solution. Open literals of the other type are
   ignored there because they are removed by universal (resp. existential)
   reduction of the constraint restricted to its non-dead literals.
   That reduction is only allowed while the other-type variable is untouched
   (no value removed yet), which is what "unassigned" guarantees in Boolean
   QBF. An open other-type literal on a partly narrowed variable blocks the
   constraint (NONE) until the variable is narrowed further; otherwise learning
   can get stuck on tautologies (see tests/cases/17_*). */
static Eval
evaluate (const Solver * s, const Constraint * c)
{
  QType own = c->is_cube ? QTYPE_FORALL : QTYPE_EXISTS;
  int n_own_open = 0, own_open = -1, i;
  int min_other_block = 1 << 30;
  Eval r = { EV_NONE, -1 };
  for (i = 0; i < c->nlits; i++)
    {
      const Lit *l = c->lits + i;
      const Var *v = s->vars + l->var;
      if (!bits_intersects (v->cur, l->set, v->nw))
        continue;               /* DEAD */
      if (bits_subset (v->cur, l->set, v->nw))
        {
          r.kind = EV_SATISFIED; /* FULL */
          return r;
        }
      if (v->qtype == own)
        {
          if (++n_own_open > 1)
            return r;
          own_open = i;
        }
      else
        {
          if (bits_count (v->cur, v->nw) != v->domain)
            return r;           /* touched */
          if (v->block < min_other_block)
            min_other_block = v->block;
        }
    }
  if (n_own_open == 0)
    r.kind = EV_EMPTY;
  else if (min_other_block > s->vars[c->lits[own_open].var].block)
    {
      r.kind = EV_UNIT;
      r.lit = own_open;
    }
  return r;
}

/* Restricts cur(var) to cur(var) & keep and records the removed values on the trail. */
static void
narrow (Solver * s, int var, const word * keep, int reason)
{
  Var *v = s->vars + var;
  int idx = s->trail.n, off = s->trail_words.n, val;
  Event e;
  word *removed;
  VEC_RESERVE (s->trail_words, off + v->nw);
  s->trail_words.n += v->nw;
  removed = s->trail_words.a + off;
  bits_minus (removed, v->cur, keep, v->nw);
  assert (!bits_empty (removed, v->nw));
  bits_and (v->cur, keep, v->nw);
  assert (!bits_empty (v->cur, v->nw));
  BITS_FOREACH (removed, v->nw, val) v->rm[val] = idx;
  e.var = var;
  e.removed = off;
  e.reason = reason;
  e.level = s->level;
  VEC_PUSH (s->trail, e);
  if (reason >= 0)
    {
      s->cons.a[reason].reason_refs++;
      s->stats.propagations++;
    }
  if (bits_count (v->cur, v->nw) == 1)
    v->saved = bits_first (v->cur, v->nw);
}

static int
propagate (Solver * s)
{
  while (s->qhead < s->trail.n)
    {
      int x = s->trail.a[s->qhead++].var, i;
      for (i = 0; i < s->vars[x].occs.n; i++)
        {
          int cid = s->vars[x].occs.a[i];
          const Constraint *c = s->cons.a + cid;
          Eval e;
          if (c->deleted)
            continue;
          e = evaluate (s, c);
          if (e.kind == EV_EMPTY)
            return cid;
          if (e.kind == EV_UNIT)
            narrow (s, c->lits[e.lit].var, c->lits[e.lit].set, cid);
        }
    }
  return -1;
}

static int
propagate_all (Solver * s)
{
  int cid;
  for (cid = 0; cid < s->cons.n; cid++)
    {
      const Constraint *c = s->cons.a + cid;
      Eval e;
      if (c->deleted)
        continue;
      e = evaluate (s, c);
      if (e.kind == EV_EMPTY)
        return cid;
      if (e.kind == EV_UNIT)
        narrow (s, c->lits[e.lit].var, c->lits[e.lit].set, cid);
    }
  return propagate (s);
}

/* ------------------------------------------------------------------------- */
/* Decisions and backtracking                                                */
/* ------------------------------------------------------------------------- */

/* Linear prefix order (DepQBF --dep-man=simple): decide a variable of the
   outermost block that still has unassigned variables; inside the block take
   the most active one. The variable is fixed to one value. */
static void
decide (Solver * s)
{
  int best = -1, b, i, val;
  Var *v;
  for (b = 0; b < s->block_vars.n && best == -1; b++)
    for (i = 0; i < s->block_vars.a[b].n; i++)
      {
        int x = s->block_vars.a[b].a[i];
        if (bits_count (s->vars[x].cur, s->vars[x].nw) > 1
            && (best == -1 || s->vars[x].activity > s->vars[best].activity))
          best = x;
      }
  assert (best != -1);
  v = s->vars + best;
  val = (v->saved >= 0 && bits_test (v->cur, v->saved)) ? v->saved : bits_first (v->cur, v->nw);
  s->level++;
  VEC_PUSH (s->level_start, s->trail.n);
  VEC_PUSH (s->dec_qtype, v->qtype);
  bits_clear_all (s->tmp, v->nw);
  bits_set (s->tmp, val);
  s->stats.decisions++;
  if (s->verbosity > 2)
    fprintf (stderr, "decide %d=%d at level %d\n", best, val, s->level);
  narrow (s, best, s->tmp, -1);
}

static void
backtrack (Solver * s, int lvl)
{
  int start, val;
  if (s->level <= lvl)
    return;
  start = s->level_start.a[lvl + 1];
  while (s->trail.n > start)
    {
      Event *e = s->trail.a + s->trail.n - 1;
      Var *v = s->vars + e->var;
      word *removed = event_removed (s, e);
      bits_or (v->cur, removed, v->nw);
      BITS_FOREACH (removed, v->nw, val) v->rm[val] = -1;
      if (e->reason >= 0)
        s->cons.a[e->reason].reason_refs--;
      s->trail_words.n = e->removed;
      s->trail.n--;
    }
  s->level_start.n = lvl + 1;
  s->dec_qtype.n = lvl + 1;
  s->level = lvl;
  s->qhead = s->trail.n;
}

static int
all_clauses_satisfied (const Solver * s)
{
  int i, j;
  for (i = 0; i < s->original_clauses.n; i++)
    {
      const Constraint *c = s->cons.a + s->original_clauses.a[i];
      int sat = 0;
      for (j = 0; j < c->nlits && !sat; j++)
        {
          const Var *v = s->vars + c->lits[j].var;
          if (bits_subset (v->cur, c->lits[j].set, v->nw))
            sat = 1;
        }
      if (!sat)
        return 0;
    }
  return 1;
}

/* A list of literals owned by the caller (used for the initial cube). */
typedef struct
{
  int n;
  int *vars;
  word **sets;
} LitList;

static void
litlist_free (LitList * l)
{
  int i;
  for (i = 0; i < l->n; i++)
    free (l->sets[i]);
  free (l->vars);
  free (l->sets);
}

/* Builds a cube that implies the matrix under the current (satisfying)
   assignment: for each clause pick a true literal (x in S); the cube contains
   x in (intersection of all S picked for x). Preference: a variable already in
   the cube whose set already fits, then any variable already in the cube, then
   an existential literal (removable by existential reduction), then any. */
static LitList
initial_cube (Solver * s)
{
  LitList cube;
  int *pos = xrealloc (0, (s->nvars + 1) * sizeof (int));
  int i, j, r, x;
  cube.n = 0;
  cube.vars = xrealloc (0, (s->nvars + 1) * sizeof (int));
  cube.sets = xrealloc (0, (s->nvars + 1) * sizeof (word *));
  for (x = 0; x <= s->nvars; x++)
    pos[x] = -1;
  for (i = 0; i < s->original_clauses.n; i++)
    {
      const Constraint *c = s->cons.a + s->original_clauses.a[i];
      int pick[4] = { -1, -1, -1, -1 }, k = -1;
      const Lit *l;
      for (j = 0; j < c->nlits; j++)
        {
          const Lit *lj = c->lits + j;
          const Var *v = s->vars + lj->var;
          int p = pos[lj->var], rank;
          if (!bits_subset (v->cur, lj->set, v->nw))
            continue;
          if (p >= 0)
            rank = bits_subset (cube.sets[p], lj->set, v->nw) ? 0 : 1;
          else
            rank = v->qtype == QTYPE_EXISTS ? 2 : 3;
          if (pick[rank] == -1)
            pick[rank] = j;
        }
      for (r = 0; r < 4 && k == -1; r++)
        k = pick[r];
      assert (k != -1);
      l = c->lits + k;
      if (pos[l->var] >= 0)
        bits_and (cube.sets[pos[l->var]], l->set, s->vars[l->var].nw);
      else
        {
          int nw = s->vars[l->var].nw;
          pos[l->var] = cube.n;
          cube.vars[cube.n] = l->var;
          cube.sets[cube.n] = xrealloc (0, nw * sizeof (word));
          bits_copy (cube.sets[cube.n], l->set, nw);
          cube.n++;
        }
    }
  for (i = 0; i < cube.n; i++)
    {
      const Var *v = s->vars + cube.vars[i];
      bits_fill (s->tmp, v->domain);
      bits_minus (cube.sets[i], s->tmp, cube.sets[i], v->nw);
    }
  free (pos);
  return cube;
}

/* ------------------------------------------------------------------------- */
/* Learning (Q-resolution on the working constraint W)                       */
/* ------------------------------------------------------------------------- */

static void
w_add (Solver * s, int var, const word * set)
{
  int nw = s->vars[var].nw;
  if (s->in_w[var])
    bits_or (s->wset[var], set, nw);
  else
    {
      s->in_w[var] = 1;
      bits_copy (s->wset[var], set, nw);
      VEC_PUSH (s->wvars, var);
    }
  assert (!bits_is_full (s->wset[var], s->vars[var].domain));
}

static void
w_remove (Solver * s, int var)
{
  int i;
  s->in_w[var] = 0;
  for (i = 0; s->wvars.a[i] != var; i++)
    ;
  for (; i + 1 < s->wvars.n; i++)
    s->wvars.a[i] = s->wvars.a[i + 1];
  s->wvars.n--;
}

static void
w_clear (Solver * s)
{
  int i;
  for (i = 0; i < s->wvars.n; i++)
    s->in_w[s->wvars.a[i]] = 0;
  s->wvars.n = 0;
}

/* Universal reduction for clauses / existential reduction for cubes. */
static void
w_reduce (Solver * s, QType own)
{
  int maxb = -1, i, k = 0;
  for (i = 0; i < s->wvars.n; i++)
    {
      const Var *v = s->vars + s->wvars.a[i];
      if (v->qtype == own && v->block > maxb)
        maxb = v->block;
    }
  for (i = 0; i < s->wvars.n; i++)
    {
      int x = s->wvars.a[i];
      if (s->vars[x].qtype == own || s->vars[x].block < maxb)
        s->wvars.a[k++] = x;
      else
        s->in_w[x] = 0;
    }
  s->wvars.n = k;
}

/* Trail index of the latest event that removed a value of b from var
   (only events with a constraint reason if propagated_only); -1 if none. */
static int
last_removal (const Solver * s, int var, const word * b, int propagated_only)
{
  const Var *v = s->vars + var;
  int best = -1, val;
  BITS_FOREACH (b, v->nw, val)
  {
    int e = v->rm[val];
    if (e >= 0 && (!propagated_only || s->trail.a[e].reason >= 0) && e > best)
      best = e;
  }
  return best;
}

/* Decision level at which literal (var, b) became DEAD. Requires it to be dead. */
static int
dead_level (const Solver * s, int var, const word * b)
{
  const Var *v = s->vars + var;
  int lvl = 0, val;
  BITS_FOREACH (b, v->nw, val)
  {
    assert (v->rm[val] >= 0);
    if (s->trail.a[v->rm[val]].level > lvl)
      lvl = s->trail.a[v->rm[val]].level;
  }
  return lvl;
}

/* DepQBF's stop criterion: exactly one own-type literal A at the maximal
   level d, d is a decision level of the own type, and every other-type
   literal left of A is dead below d. Additionally, every other-type literal
   right of A must be dead or on an untouched variable at the backtrack level;
   otherwise evaluate() would not see the learned constraint as unit there
   (see tests/cases/18_*). */
static int
w_stop (Solver * s, QType own, int *bt_level)
{
  int dmax = -1, cnt = 0, a = -1, bt = 0, i;
  for (i = 0; i < s->wvars.n; i++)
    {
      int x = s->wvars.a[i], d;
      if (s->vars[x].qtype != own)
        continue;
      d = dead_level (s, x, s->wset[x]);
      if (d > dmax)
        {
          dmax = d;
          cnt = 1;
          a = x;
        }
      else if (d == dmax)
        cnt++;
    }
  if (cnt != 1 || dmax == 0 || s->dec_qtype.a[dmax] != own)
    return 0;
  for (i = 0; i < s->wvars.n; i++)
    {
      int x = s->wvars.a[i], d;
      const Var *v = s->vars + x;
      if (x == a)
        continue;
      if (v->qtype == own)
        {
          d = dead_level (s, x, s->wset[x]);
          if (d > bt)
            bt = d;
        }
      else if (v->block < s->vars[a].block)
        {
          if (bits_intersects (v->cur, s->wset[x], v->nw))
            return 0;           /* not dead */
          d = dead_level (s, x, s->wset[x]);
          if (d >= dmax)
            return 0;
          if (d > bt)
            bt = d;
        }
    }
  for (i = 0; i < s->wvars.n; i++)
    {
      int x = s->wvars.a[i];
      const Var *v = s->vars + x;
      int touched = 0, dead = 1, val;
      if (v->qtype == own || v->block < s->vars[a].block)
        continue;
      for (val = 0; val < v->domain; val++)
        {
          int e = v->rm[val];
          int gone = e >= 0 && s->trail.a[e].level <= bt;  /* removed at level <= bt */
          if (gone)
            touched = 1;
          if (bits_test (s->wset[x], val) && !gone)
            dead = 0;
        }
      if (touched && !dead)
        return 0;
    }
  *bt_level = bt;
  return 1;
}

/* Returns an other-type variable on which resolving W with 'reason' on 'pivot'
   would produce a tautology (complementary sets covering the domain), or 0. */
static int
would_be_tautology (Solver * s, const Constraint * reason, int pivot, QType own)
{
  int i;
  for (i = 0; i < reason->nlits; i++)
    {
      const Lit *l = reason->lits + i;
      const Var *v = s->vars + l->var;
      if (l->var == pivot || v->qtype == own || !s->in_w[l->var])
        continue;
      bits_copy (s->tmp, s->wset[l->var], v->nw);
      bits_or (s->tmp, l->set, v->nw);
      if (bits_is_full (s->tmp, v->domain))
        return l->var;
    }
  return 0;
}

/* Pivot selection as in DepQBF's choose_var: the latest propagation event on
   an own-type literal of W; if that would give a tautology on variable u, the
   latest one among own-type variables right of u that gives no tautology. */
static int
w_pivot (Solver * s, QType own)
{
  int best = -1, blocking, i;
  for (i = 0; i < s->wvars.n; i++)
    {
      int x = s->wvars.a[i];
      if (s->vars[x].qtype == own)
        {
          int e = last_removal (s, x, s->wset[x], 1);
          if (e > best)
            best = e;
        }
    }
  if (best < 0)
    return -1;
  blocking = would_be_tautology (s, s->cons.a + s->trail.a[best].reason, s->trail.a[best].var, own);
  if (!blocking)
    return best;
  best = -1;
  for (i = 0; i < s->wvars.n; i++)
    {
      int x = s->wvars.a[i], e;
      if (s->vars[x].qtype != own || s->vars[x].block <= s->vars[blocking].block)
        continue;
      e = last_removal (s, x, s->wset[x], 1);
      if (e > best && !would_be_tautology (s, s->cons.a + s->trail.a[e].reason, x, own))
        best = e;
    }
  return best;
}

static void
bump_var (Solver * s, int var)
{
  int x;
  if ((s->vars[var].activity += s->var_inc) > 1e100)
    {
      for (x = 0; x <= s->nvars; x++)
        s->vars[x].activity *= 1e-100;
      s->var_inc *= 1e-100;
    }
}

/* Returns 1 if a learned constraint was derived (it is left in W, the
   asserting level in *bt_level) and 0 if the empty constraint was derived. */
static int
analyze (Solver * s, int n, const int *start_vars, word * const *start_sets, int is_cube, int *bt_level)
{
  QType own = is_cube ? QTYPE_FORALL : QTYPE_EXISTS;
  int i;
  w_clear (s);
  for (i = 0; i < n; i++)
    w_add (s, start_vars[i], start_sets[i]);
  w_reduce (s, own);
  for (;;)
    {
      int p, x;
      const Constraint *r;
      if (!s->wvars.n)
        return 0;
      if (w_stop (s, own, bt_level))
        break;
      p = w_pivot (s, own);
      if (p < 0)
        internal_error ("no pivot found during learning");
      x = s->trail.a[p].var;
      r = s->cons.a + s->trail.a[p].reason;
      for (i = 0; i < r->nlits; i++)
        if (r->lits[i].var == x)
          bits_and (s->wset[x], r->lits[i].set, s->vars[x].nw);
      if (bits_empty (s->wset[x], s->vars[x].nw))
        w_remove (s, x);
      for (i = 0; i < r->nlits; i++)
        if (r->lits[i].var != x)
          w_add (s, r->lits[i].var, r->lits[i].set);
      w_reduce (s, own);
    }
  for (i = 0; i < s->wvars.n; i++)
    bump_var (s, s->wvars.a[i]);
  s->var_inc /= 0.95;
  return 1;
}

/* Deletes the older half of the learned constraints that are not reasons. */
static void
reduce_learned (Solver * s)
{
  IntVec cand = { 0, 0, 0 };
  int cid, i, x;
  for (cid = 0; cid < s->cons.n; cid++)
    {
      const Constraint *c = s->cons.a + cid;
      if (c->learned && !c->deleted && c->reason_refs == 0)
        VEC_PUSH (cand, cid);
    }
  for (i = 0; i < cand.n / 2; i++)
    {
      Constraint *c = s->cons.a + cand.a[i];
      c->deleted = 1;
      free (c->lits);
      free (c->words);
      c->lits = 0;
      c->words = 0;
      c->nlits = 0;
      s->num_learned--;
      s->stats.deleted++;
    }
  free (cand.a);
  for (x = 1; x <= s->nvars; x++)
    {
      IntVec *o = &s->vars[x].occs;
      int k = 0;
      for (i = 0; i < o->n; i++)
        if (!s->cons.a[o->a[i]].deleted)
          o->a[k++] = o->a[i];
      o->n = k;
    }
}

static long long
luby (long long i)
{
  for (;;)
    {
      int k = 1;
      while ((1LL << k) - 1 < i)
        k++;
      if ((1LL << k) - 1 == i)
        return 1LL << (k - 1);
      i = i - (1LL << (k - 1)) + 1;
    }
}

/* ------------------------------------------------------------------------- */
/* Printing                                                                  */
/* ------------------------------------------------------------------------- */

static void
print_constraint (Solver * s, FILE * f, const int *vs, word * const *sets, int n, int is_cube)
{
  int i, val;
  fputc (is_cube ? '[' : '(', f);
  for (i = 0; i < n; i++)
    {
      const Var *v = s->vars + vs[i];
      int first = 1;
      if (i)
        fputs (is_cube ? " & " : " | ", f);
      if (is_cube)
        {
          bits_fill (s->tmp, v->domain);
          bits_minus (s->tmp, s->tmp, sets[i], v->nw);
        }
      else
        bits_copy (s->tmp, sets[i], v->nw);
      fprintf (f, "%d:", vs[i]);
      BITS_FOREACH (s->tmp, v->nw, val)
      {
        fprintf (f, first ? "%d" : ",%d", val);
        first = 0;
      }
    }
  fputc (is_cube ? ']' : ')', f);
}

/* ------------------------------------------------------------------------- */
/* Main loop                                                                 */
/* ------------------------------------------------------------------------- */

Result
solver_solve (Solver * s)
{
  int cid, x;
  IntVec lv = { 0, 0, 0 };
  VEC (word *) ls = { 0, 0, 0 };
  Result result;

  finish_prefix (s);
  if (s->empty_clause_seen)
    return RESULT_FALSE;
  s->wset = xrealloc (0, (s->nvars + 1) * sizeof (word *));
  s->in_w = xrealloc (0, s->nvars + 1);
  for (x = 0; x <= s->nvars; x++)
    {
      s->wset[x] = xrealloc (0, (x ? s->vars[x].nw : 1) * sizeof (word));
      s->in_w[x] = 0;
    }
  s->level_start.n = 0;
  VEC_PUSH (s->level_start, 0);
  s->dec_qtype.n = 0;
  VEC_PUSH (s->dec_qtype, QTYPE_EXISTS);
  s->conflicts_until_restart = luby (++s->luby_index) * s->restart_base;

  cid = propagate_all (s);
  for (;;)
    {
      int is_cube, bt, ok, nid, i;
      Eval e;
      if (cid >= 0)
        {
          /* The literals of 'cid' are copied into W before anything changes. */
          const Constraint *c = s->cons.a + cid;
          is_cube = c->is_cube;
          lv.n = ls.n = 0;
          for (i = 0; i < c->nlits; i++)
            {
              VEC_PUSH (lv, c->lits[i].var);
              VEC_PUSH (ls, c->lits[i].set);
            }
          if (is_cube)
            s->stats.solutions++;
          else
            s->stats.conflicts++;
          ok = analyze (s, lv.n, lv.a, ls.a, is_cube, &bt);
        }
      else if (all_clauses_satisfied (s))
        {
          LitList cube = initial_cube (s);
          is_cube = 1;
          s->stats.solutions++;
          ok = analyze (s, cube.n, cube.vars, cube.sets, is_cube, &bt);
          litlist_free (&cube);
        }
      else
        {
          decide (s);
          cid = propagate (s);
          continue;
        }

      if (!ok)
        {
          result = is_cube ? RESULT_TRUE : RESULT_FALSE;
          break;
        }

      /* The learned constraint is the working constraint W. */
      lv.n = ls.n = 0;
      for (i = 0; i < s->wvars.n; i++)
        {
          VEC_PUSH (lv, s->wvars.a[i]);
          VEC_PUSH (ls, s->wset[s->wvars.a[i]]);
        }
      if (s->verbosity > 1)
        {
          fprintf (stderr, "learned %s ", is_cube ? "cube" : "clause");
          print_constraint (s, stderr, lv.a, ls.a, lv.n, is_cube);
          fprintf (stderr, ", backtrack %d -> %d\n", s->level, bt);
        }

      backtrack (s, bt);
      s->stats.backjumps++;
      nid = add_constraint (s, lv.a, ls.a, lv.n, is_cube, 1);
      e = evaluate (s, s->cons.a + nid);
      if (e.kind != EV_UNIT)
        internal_error ("learned constraint is not asserting");
      narrow (s, s->cons.a[nid].lits[e.lit].var, s->cons.a[nid].lits[e.lit].set, nid);

      if (--s->conflicts_until_restart <= 0)
        {
          backtrack (s, 0);
          s->stats.restarts++;
          s->conflicts_until_restart = luby (++s->luby_index) * s->restart_base;
        }
      if (s->num_learned > s->max_learned)
        {
          reduce_learned (s);
          s->max_learned += s->max_learned / 10 > 1 ? s->max_learned / 10 : 1;
        }
      cid = propagate (s);
    }
  free (lv.a);
  free (ls.a);
  return result;
}
