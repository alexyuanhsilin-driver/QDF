// DiscQBF: a QCDCL solver for QBFs over discrete (multi-valued) variables.
//
// The search follows DepQBF's QCDCL with the "simple" dependency manager:
// decisions respect the linear quantifier prefix, conflicts produce learned
// clauses and solutions produce learned cubes via traditional Q-resolution
// (no long-distance resolution, no dependency schemes, no generalized axioms).
//
// Literals are DSAT-style: a literal is a pair (x, S) where S is a set of
// values of variable x.
//
// Unified representation (as in DepQBF, clauses and cubes share one engine):
// every constraint literal stores a value set B, interpreted as follows.
//   clause literal x in S  ->  B = S
//   cube   literal x in S  ->  B = dom(x) \ S
// With cur(x) being the values x may still take, a literal is
//   DEAD  if cur(x) & B == {}   (clause literal false / cube literal true)
//   FULL  if cur(x) is a subset of B (clause literal true / cube literal false)
//   OPEN  otherwise.
// A constraint's "own" quantifier type is EXISTS for clauses and FORALL for
// cubes. Unit propagation of an open own-type literal (x, B) always narrows
// cur(x) to cur(x) & B; resolution on a pivot always intersects the pivot
// sets and unions all other sets. This lets one piece of code do both CDCL
// (clauses) and SDCL (cubes).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Bits.hpp"

namespace discqbf {

enum QType { EXISTS = 0, FORALL = 1 };
enum Result { RESULT_UNKNOWN = 0, RESULT_TRUE = 10, RESULT_FALSE = 20 };

struct Lit {
    int var;
    Bits set;  // the stored set B (see header comment)
};

struct Constraint {
    std::vector<Lit> lits;  // one literal per variable, sorted by (block, var)
    bool is_cube = false;
    bool learned = false;
    bool deleted = false;
    int reason_refs = 0;  // number of trail events using this as reason
};

struct Var {
    int domain = 2;
    QType qtype = EXISTS;
    int block = 0;        // index of quantifier block, 0 = outermost
    Bits cur;             // values still possible
    std::vector<int> rm;  // rm[v] = trail index of event that removed v, -1 if present
    double activity = 0;
    int saved = -1;       // phase saving: last value the variable was fixed to
    std::vector<int> occs;  // constraints (ids) containing this variable
};

// One entry on the trail: values removed from a variable's domain.
struct Event {
    int var;
    Bits removed;
    int reason;  // constraint id, or -1 for a decision
    int level;
};

struct Stats {
    long long decisions = 0, propagations = 0, conflicts = 0, solutions = 0;
    long long restarts = 0, deleted = 0, backjumps = 0;
};

class Solver {
   public:
    // ---- formula construction (used by the parser) ----
    // Variables are numbered 1..n. Call setNumVars first, then setDomain,
    // addBlock (outermost first) and finally addClause.
    void setNumVars(int n);
    void setDomain(int var, int domain);
    void addBlock(QType q, const std::vector<int>& vars);
    // Literals given as (var, set S of values). Duplicate variables are merged.
    void addClause(std::vector<std::pair<int, Bits>> lits);
    int domain(int var) const { return vars[var].domain; }
    int numVars() const { return (int)vars.size() - 1; }

    Result solve();

    Stats stats;
    int verbosity = 0;
    int restart_base = 100;  // conflicts (+ solutions) per Luby restart unit
    int max_learned = 2000;  // learned constraints kept before the first reduction

   private:
    std::vector<Var> vars;  // index 0 unused
    std::vector<QType> block_qtype;
    std::vector<std::vector<int>> block_vars;
    bool prefix_done = false;
    bool empty_clause_seen = false;

    std::vector<Constraint> cons;
    std::vector<int> original_clauses;
    int num_learned = 0;

    std::vector<Event> trail;
    size_t qhead = 0;              // next trail event to propagate
    std::vector<QType> dec_qtype;  // dec_qtype[l] = qtype of decision at level l (l >= 1)
    std::vector<size_t> level_start;
    int level = 0;

    double var_inc = 1.0;
    long long luby_index = 0, conflicts_until_restart = 0;

    // Working constraint for learning, indexed by variable.
    std::vector<Bits> wset;
    std::vector<char> in_w;
    std::vector<int> wvars;

    enum EvalKind { EV_NONE, EV_SATISFIED, EV_UNIT, EV_EMPTY };
    struct Eval {
        EvalKind kind;
        int lit;  // index of the unit literal
    };

    void finishPrefix();
    int addConstraint(std::vector<Lit> lits, bool is_cube, bool learned);
    Eval evaluate(const Constraint& c) const;
    void narrow(int var, const Bits& keep, int reason);
    int propagate();  // returns id of empty constraint or -1
    int propagateAll();
    void decide();
    void backtrack(int lvl);
    bool allClausesSatisfied() const;
    std::vector<Lit> initialCube();

    // Analysis: returns true and fills 'out' (+ asserting level) if a learned
    // constraint was derived; returns false if the empty constraint was derived.
    bool analyze(std::vector<Lit> start, bool is_cube, std::vector<Lit>& out, int& bt_level);
    void wAdd(const Lit& l);
    void wRemove(int var);
    void wReduce(QType own);
    void wClear();
    bool wStop(QType own, int& bt_level);
    int wPivot(QType own);
    int wouldBeTautology(const Constraint& reason, int pivot, QType own) const;
    int lastRemoval(int var, const Bits& b, bool propagated_only) const;
    int deadLevel(int var, const Bits& b) const;

    void bumpVar(int var);
    void reduceLearned();
    static long long luby(long long i);

   public:
    std::string litToString(const Lit& l, bool is_cube) const;
    std::string constraintToString(const std::vector<Lit>& lits, bool is_cube) const;
};

}  // namespace discqbf
