#include "Solver.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <stdexcept>

namespace discqbf {

// ---------------------------------------------------------------------------
// Formula construction
// ---------------------------------------------------------------------------

void Solver::setNumVars(int n) {
    vars.assign(n + 1, Var());
    for (int x = 1; x <= n; x++) vars[x].block = -1;  // -1 = not quantified yet
}

void Solver::setDomain(int var, int domain) {
    if (var < 1 || var > numVars()) throw std::runtime_error("variable out of range: " + std::to_string(var));
    if (domain < 1) throw std::runtime_error("domain size must be positive");
    if (prefix_done) throw std::runtime_error("domain sizes must be given before the clauses");
    vars[var].domain = domain;
}

void Solver::addBlock(QType q, const std::vector<int>& vs) {
    if (prefix_done) throw std::runtime_error("quantifier blocks must be given before the clauses");
    if (block_qtype.empty() || block_qtype.back() != q) {
        block_qtype.push_back(q);
        block_vars.emplace_back();
    }
    int b = (int)block_qtype.size() - 1;
    for (int x : vs) {
        if (x < 1 || x > numVars()) throw std::runtime_error("variable out of range: " + std::to_string(x));
        if (vars[x].block != -1) throw std::runtime_error("variable quantified twice: " + std::to_string(x));
        vars[x].block = b;
        vars[x].qtype = q;
        block_vars[b].push_back(x);
    }
}

// Unquantified (free) variables go to an outermost existential block, as in DepQBF.
void Solver::finishPrefix() {
    if (prefix_done) return;
    prefix_done = true;
    std::vector<int> free_vars;
    for (int x = 1; x <= numVars(); x++)
        if (vars[x].block == -1) free_vars.push_back(x);
    if (!free_vars.empty()) {
        if (block_qtype.empty() || block_qtype[0] != EXISTS) {
            block_qtype.insert(block_qtype.begin(), EXISTS);
            block_vars.insert(block_vars.begin(), std::vector<int>());
            for (int x = 1; x <= numVars(); x++)
                if (vars[x].block != -1) vars[x].block++;
        }
        for (int x : free_vars) {
            vars[x].block = 0;
            vars[x].qtype = EXISTS;
            block_vars[0].push_back(x);
        }
    }
    for (auto& bv : block_vars) std::sort(bv.begin(), bv.end());
    for (int x = 1; x <= numVars(); x++) {
        Var& v = vars[x];
        v.cur = Bits::full(v.domain);
        v.rm.assign(v.domain, -1);
    }
}

void Solver::addClause(std::vector<std::pair<int, Bits>> in) {
    finishPrefix();
    // Merge literals on the same variable: (x in S1) or (x in S2) = x in S1|S2.
    std::vector<Lit> lits;
    for (auto& [x, s] : in) {
        if (s.empty()) continue;  // x in {} is false
        bool merged = false;
        for (auto& l : lits)
            if (l.var == x) {
                l.set |= s;
                merged = true;
            }
        if (!merged) lits.push_back({x, s});
    }
    for (auto& l : lits)
        if (l.set == Bits::full(vars[l.var].domain)) return;  // tautology

    // Universal reduction: drop universal literals right of all existential ones.
    int maxb = -1;
    for (auto& l : lits)
        if (vars[l.var].qtype == EXISTS) maxb = std::max(maxb, vars[l.var].block);
    std::vector<Lit> kept;
    for (auto& l : lits)
        if (vars[l.var].qtype == EXISTS || vars[l.var].block < maxb) kept.push_back(l);

    if (kept.empty()) {
        empty_clause_seen = true;
        return;
    }
    original_clauses.push_back(addConstraint(kept, false, false));
}

int Solver::addConstraint(std::vector<Lit> lits, bool is_cube, bool learned) {
    std::sort(lits.begin(), lits.end(), [&](const Lit& a, const Lit& b) {
        if (vars[a.var].block != vars[b.var].block) return vars[a.var].block < vars[b.var].block;
        return a.var < b.var;
    });
    int id = (int)cons.size();
    Constraint c;
    c.lits = std::move(lits);
    c.is_cube = is_cube;
    c.learned = learned;
    for (auto& l : c.lits) vars[l.var].occs.push_back(id);
    cons.push_back(std::move(c));
    if (learned) num_learned++;
    return id;
}

// ---------------------------------------------------------------------------
// Propagation
// ---------------------------------------------------------------------------

// Classifies a constraint under the current domains (see the header comment).
// EMPTY means: no FULL literal and no OPEN own-type literal; for a clause that
// is a conflict, for a cube a solution. Open literals of the other type are
// ignored there because they are removed by universal (resp. existential)
// reduction of the constraint restricted to its non-dead literals.
// That reduction is only allowed while the other-type variable is untouched
// (no value removed yet), which is what "unassigned" guarantees in Boolean
// QBF. An open other-type literal on a partly narrowed variable blocks the
// constraint (NONE) until the variable is narrowed further; otherwise learning
// can get stuck on tautologies (see tests/cases/17_*).
Solver::Eval Solver::evaluate(const Constraint& c) const {
    QType own = c.is_cube ? FORALL : EXISTS;
    int n_own_open = 0, own_open = -1;
    int min_other_block = 1 << 30;
    for (int i = 0; i < (int)c.lits.size(); i++) {
        const Lit& l = c.lits[i];
        const Var& v = vars[l.var];
        if (!v.cur.intersects(l.set)) continue;          // DEAD
        if (v.cur.subsetOf(l.set)) return {EV_SATISFIED, -1};  // FULL
        if (v.qtype == own) {
            if (++n_own_open > 1) return {EV_NONE, -1};
            own_open = i;
        } else {
            if (v.cur.count() != v.domain) return {EV_NONE, -1};  // touched
            min_other_block = std::min(min_other_block, v.block);
        }
    }
    if (n_own_open == 0) return {EV_EMPTY, -1};
    if (min_other_block > vars[c.lits[own_open].var].block) return {EV_UNIT, own_open};
    return {EV_NONE, -1};
}

// Restricts cur(var) to cur(var) & keep and records the removed values on the trail.
void Solver::narrow(int var, const Bits& keep, int reason) {
    Var& v = vars[var];
    Bits removed = v.cur.minus(keep);
    assert(!removed.empty());
    v.cur &= keep;
    assert(!v.cur.empty());
    int idx = (int)trail.size();
    removed.forEach([&](int val) { v.rm[val] = idx; });
    trail.push_back({var, removed, reason, level});
    if (reason >= 0) {
        cons[reason].reason_refs++;
        stats.propagations++;
    }
    if (v.cur.count() == 1) v.saved = v.cur.first();
}

int Solver::propagate() {
    while (qhead < trail.size()) {
        int x = trail[qhead++].var;
        for (int cid : vars[x].occs) {
            const Constraint& c = cons[cid];
            if (c.deleted) continue;
            Eval e = evaluate(c);
            if (e.kind == EV_EMPTY) return cid;
            if (e.kind == EV_UNIT) narrow(c.lits[e.lit].var, c.lits[e.lit].set, cid);
        }
    }
    return -1;
}

int Solver::propagateAll() {
    for (int cid = 0; cid < (int)cons.size(); cid++) {
        const Constraint& c = cons[cid];
        if (c.deleted) continue;
        Eval e = evaluate(c);
        if (e.kind == EV_EMPTY) return cid;
        if (e.kind == EV_UNIT) narrow(c.lits[e.lit].var, c.lits[e.lit].set, cid);
    }
    return propagate();
}

// ---------------------------------------------------------------------------
// Decisions and backtracking
// ---------------------------------------------------------------------------

// Linear prefix order (DepQBF --dep-man=simple): decide a variable of the
// outermost block that still has unassigned variables; inside the block take
// the most active one. The variable is fixed to one value.
void Solver::decide() {
    int best = -1;
    for (auto& bv : block_vars) {
        for (int x : bv)
            if (vars[x].cur.count() > 1 && (best == -1 || vars[x].activity > vars[best].activity)) best = x;
        if (best != -1) break;
    }
    assert(best != -1);
    Var& v = vars[best];
    int val = (v.saved >= 0 && v.cur.test(v.saved)) ? v.saved : v.cur.first();
    level++;
    level_start.push_back(trail.size());
    dec_qtype.push_back(v.qtype);
    Bits keep(Bits::wordsFor(v.domain));
    keep.set(val);
    stats.decisions++;
    if (verbosity > 2) fprintf(stderr, "decide %d=%d at level %d\n", best, val, level);
    narrow(best, keep, -1);
}

void Solver::backtrack(int lvl) {
    if (level <= lvl) return;
    size_t start = level_start[lvl + 1];
    while (trail.size() > start) {
        Event& e = trail.back();
        Var& v = vars[e.var];
        v.cur |= e.removed;
        e.removed.forEach([&](int val) { v.rm[val] = -1; });
        if (e.reason >= 0) cons[e.reason].reason_refs--;
        trail.pop_back();
    }
    level_start.resize(lvl + 1);
    dec_qtype.resize(lvl + 1);
    level = lvl;
    qhead = trail.size();
}

bool Solver::allClausesSatisfied() const {
    for (int cid : original_clauses) {
        bool sat = false;
        for (auto& l : cons[cid].lits)
            if (vars[l.var].cur.subsetOf(l.set)) {
                sat = true;
                break;
            }
        if (!sat) return false;
    }
    return true;
}

// Builds a cube that implies the matrix under the current (satisfying)
// assignment: for each clause pick a true literal (x in S); the cube contains
// x in (intersection of all S picked for x). Preference: a variable already in
// the cube whose set already fits, then any variable already in the cube, then
// an existential literal (removable by existential reduction), then any.
std::vector<Lit> Solver::initialCube() {
    std::vector<int> pos(vars.size(), -1);
    std::vector<Lit> cube;  // stored with S (not complemented) while building
    for (int cid : original_clauses) {
        const Constraint& c = cons[cid];
        int pick[4] = {-1, -1, -1, -1};
        for (int i = 0; i < (int)c.lits.size(); i++) {
            const Lit& l = c.lits[i];
            if (!vars[l.var].cur.subsetOf(l.set)) continue;
            int p = pos[l.var];
            int rank = p >= 0 ? (cube[p].set.subsetOf(l.set) ? 0 : 1) : (vars[l.var].qtype == EXISTS ? 2 : 3);
            if (pick[rank] == -1) pick[rank] = i;
        }
        int i = -1;
        for (int r = 0; r < 4 && i == -1; r++) i = pick[r];
        assert(i != -1);
        const Lit& l = c.lits[i];
        if (pos[l.var] >= 0) {
            cube[pos[l.var]].set &= l.set;
        } else {
            pos[l.var] = (int)cube.size();
            cube.push_back(l);
        }
    }
    for (auto& l : cube) l.set = Bits::full(vars[l.var].domain).minus(l.set);
    return cube;
}

// ---------------------------------------------------------------------------
// Learning (Q-resolution on the working constraint W)
// ---------------------------------------------------------------------------

void Solver::wAdd(const Lit& l) {
    if (in_w[l.var]) {
        wset[l.var] |= l.set;
    } else {
        in_w[l.var] = 1;
        wset[l.var] = l.set;
        wvars.push_back(l.var);
    }
    assert(!(wset[l.var] == Bits::full(vars[l.var].domain)));
}

void Solver::wRemove(int var) {
    in_w[var] = 0;
    wvars.erase(std::find(wvars.begin(), wvars.end(), var));
}

void Solver::wClear() {
    for (int x : wvars) in_w[x] = 0;
    wvars.clear();
}

// Universal reduction for clauses / existential reduction for cubes.
void Solver::wReduce(QType own) {
    int maxb = -1;
    for (int x : wvars)
        if (vars[x].qtype == own) maxb = std::max(maxb, vars[x].block);
    std::vector<int> keep;
    for (int x : wvars) {
        if (vars[x].qtype == own || vars[x].block < maxb)
            keep.push_back(x);
        else
            in_w[x] = 0;
    }
    wvars = keep;
}

// Trail index of the latest event that removed a value of b from var
// (only events with a constraint reason if propagated_only); -1 if none.
int Solver::lastRemoval(int var, const Bits& b, bool propagated_only) const {
    int best = -1;
    b.forEach([&](int val) {
        int e = vars[var].rm[val];
        if (e >= 0 && (!propagated_only || trail[e].reason >= 0)) best = std::max(best, e);
    });
    return best;
}

// Decision level at which literal (var, b) became DEAD. Requires it to be dead.
int Solver::deadLevel(int var, const Bits& b) const {
    int lvl = 0;
    b.forEach([&](int val) {
        assert(vars[var].rm[val] >= 0);
        lvl = std::max(lvl, trail[vars[var].rm[val]].level);
    });
    return lvl;
}

// DepQBF's stop criterion: exactly one own-type literal A at the maximal
// level d, d is a decision level of the own type, and every other-type
// literal left of A is dead below d. Additionally, every other-type literal
// right of A must be dead or on an untouched variable at the backtrack level;
// otherwise evaluate() would not see the learned constraint as unit there
// (see tests/cases/18_*).
bool Solver::wStop(QType own, int& bt_level) {
    int dmax = -1, cnt = 0, a = -1;
    for (int x : wvars) {
        if (vars[x].qtype != own) continue;
        int d = deadLevel(x, wset[x]);
        if (d > dmax) {
            dmax = d;
            cnt = 1;
            a = x;
        } else if (d == dmax) {
            cnt++;
        }
    }
    if (cnt != 1 || dmax == 0 || dec_qtype[dmax] != own) return false;
    int bt = 0;
    for (int x : wvars) {
        if (x == a) continue;
        if (vars[x].qtype == own) {
            bt = std::max(bt, deadLevel(x, wset[x]));
        } else if (vars[x].block < vars[a].block) {
            if (vars[x].cur.intersects(wset[x])) return false;  // not dead
            int d = deadLevel(x, wset[x]);
            if (d >= dmax) return false;
            bt = std::max(bt, d);
        }
    }
    for (int x : wvars) {
        if (vars[x].qtype == own || vars[x].block < vars[a].block) continue;
        bool touched = false, dead = true;
        for (int val = 0; val < vars[x].domain; val++) {
            int e = vars[x].rm[val];
            bool gone = e >= 0 && trail[e].level <= bt;  // removed at level <= bt
            if (gone) touched = true;
            if (wset[x].test(val) && !gone) dead = false;
        }
        if (touched && !dead) return false;
    }
    bt_level = bt;
    return true;
}

// Returns an other-type variable on which resolving W with 'reason' on 'pivot'
// would produce a tautology (complementary sets covering the domain), or 0.
int Solver::wouldBeTautology(const Constraint& reason, int pivot, QType own) const {
    for (auto& l : reason.lits) {
        if (l.var == pivot || vars[l.var].qtype == own || !in_w[l.var]) continue;
        Bits u = wset[l.var];
        u |= l.set;
        if (u == Bits::full(vars[l.var].domain)) return l.var;
    }
    return 0;
}

// Pivot selection as in DepQBF's choose_var: the latest propagation event on
// an own-type literal of W; if that would give a tautology on variable u, the
// latest one among own-type variables right of u that gives no tautology.
int Solver::wPivot(QType own) {
    int best = -1;
    for (int x : wvars)
        if (vars[x].qtype == own) best = std::max(best, lastRemoval(x, wset[x], true));
    if (best < 0) return -1;
    int blocking = wouldBeTautology(cons[trail[best].reason], trail[best].var, own);
    if (!blocking) return best;
    best = -1;
    for (int x : wvars) {
        if (vars[x].qtype != own || vars[x].block <= vars[blocking].block) continue;
        int e = lastRemoval(x, wset[x], true);
        if (e > best && !wouldBeTautology(cons[trail[e].reason], x, own)) best = e;
    }
    return best;
}

bool Solver::analyze(std::vector<Lit> start, bool is_cube, std::vector<Lit>& out, int& bt_level) {
    QType own = is_cube ? FORALL : EXISTS;
    wClear();
    for (auto& l : start) wAdd(l);
    wReduce(own);
    for (;;) {
        if (wvars.empty()) return false;
        if (wStop(own, bt_level)) break;
        int p = wPivot(own);
        if (p < 0) throw std::runtime_error("internal error: no pivot found during learning");
        int x = trail[p].var;
        const Constraint& r = cons[trail[p].reason];
        if (verbosity > 2) fprintf(stderr, "resolve on %d with %s\n", x, constraintToString(r.lits, r.is_cube).c_str());
        for (auto& l : r.lits)
            if (l.var == x) wset[x] &= l.set;
        if (wset[x].empty()) wRemove(x);
        for (auto& l : r.lits)
            if (l.var != x) wAdd(l);
        wReduce(own);
    }
    out.clear();
    for (int x : wvars) {
        out.push_back({x, wset[x]});
        bumpVar(x);
    }
    var_inc /= 0.95;
    return true;
}

void Solver::bumpVar(int var) {
    if ((vars[var].activity += var_inc) > 1e100) {
        for (auto& v : vars) v.activity *= 1e-100;
        var_inc *= 1e-100;
    }
}

// Deletes the older half of the learned constraints that are not reasons.
void Solver::reduceLearned() {
    std::vector<int> cand;
    for (int cid = 0; cid < (int)cons.size(); cid++)
        if (cons[cid].learned && !cons[cid].deleted && cons[cid].reason_refs == 0) cand.push_back(cid);
    for (size_t i = 0; i < cand.size() / 2; i++) {
        Constraint& c = cons[cand[i]];
        c.deleted = true;
        c.lits.clear();
        c.lits.shrink_to_fit();
        num_learned--;
        stats.deleted++;
    }
    for (int x = 1; x <= numVars(); x++) {
        auto& o = vars[x].occs;
        o.erase(std::remove_if(o.begin(), o.end(), [&](int cid) { return cons[cid].deleted; }), o.end());
    }
}

long long Solver::luby(long long i) {
    for (;;) {
        int k = 1;
        while ((1LL << k) - 1 < i) k++;
        if ((1LL << k) - 1 == i) return 1LL << (k - 1);
        i = i - (1LL << (k - 1)) + 1;
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

Result Solver::solve() {
    finishPrefix();
    if (empty_clause_seen) return RESULT_FALSE;
    wset.assign(vars.size(), Bits());
    in_w.assign(vars.size(), 0);
    level_start.assign(1, 0);
    dec_qtype.assign(1, EXISTS);
    conflicts_until_restart = luby(++luby_index) * restart_base;

    int cid = propagateAll();
    for (;;) {
        std::vector<Lit> start;
        bool is_cube;
        if (cid >= 0) {
            start = cons[cid].lits;
            is_cube = cons[cid].is_cube;
        } else if (allClausesSatisfied()) {
            start = initialCube();
            is_cube = true;
        } else {
            decide();
            cid = propagate();
            continue;
        }

        if (is_cube)
            stats.solutions++;
        else
            stats.conflicts++;
        std::vector<Lit> learned;
        int bt;
        if (!analyze(std::move(start), is_cube, learned, bt)) return is_cube ? RESULT_TRUE : RESULT_FALSE;
        if (verbosity > 1)
            fprintf(stderr, "learned %s %s, backtrack %d -> %d\n", is_cube ? "cube" : "clause",
                    constraintToString(learned, is_cube).c_str(), level, bt);

        backtrack(bt);
        stats.backjumps++;
        int nid = addConstraint(std::move(learned), is_cube, true);
        Eval e = evaluate(cons[nid]);
        if (e.kind != EV_UNIT) throw std::runtime_error("internal error: learned constraint is not asserting");
        narrow(cons[nid].lits[e.lit].var, cons[nid].lits[e.lit].set, nid);

        if (--conflicts_until_restart <= 0) {
            backtrack(0);
            stats.restarts++;
            conflicts_until_restart = luby(++luby_index) * restart_base;
        }
        if (num_learned > max_learned) {
            reduceLearned();
            max_learned += std::max(1, max_learned / 10);
        }
        cid = propagate();
    }
}

// ---------------------------------------------------------------------------
// Printing
// ---------------------------------------------------------------------------

std::string Solver::litToString(const Lit& l, bool is_cube) const {
    Bits s = is_cube ? Bits::full(vars[l.var].domain).minus(l.set) : l.set;
    std::string r = std::to_string(l.var) + ":";
    bool first = true;
    s.forEach([&](int v) {
        if (!first) r += ",";
        r += std::to_string(v);
        first = false;
    });
    return r;
}

std::string Solver::constraintToString(const std::vector<Lit>& lits, bool is_cube) const {
    std::string r = is_cube ? "[" : "(";
    for (size_t i = 0; i < lits.size(); i++) {
        if (i) r += is_cube ? " & " : " | ";
        r += litToString(lits[i], is_cube);
    }
    return r + (is_cube ? "]" : ")");
}

}  // namespace discqbf
