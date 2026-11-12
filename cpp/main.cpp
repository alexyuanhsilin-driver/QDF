// Command line front end and DQDIMACS parser. See ../README.md for the format.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "Solver.hpp"

using namespace discqbf;

static int parseInt(const std::string& s, size_t& i) {
    if (i >= s.size() || !isdigit((unsigned char)s[i])) throw std::runtime_error("expected a number in '" + s + "'");
    long long v = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) {
        v = v * 10 + (s[i++] - '0');
        if (v > 1000000000) throw std::runtime_error("number too large in '" + s + "'");
    }
    return (int)v;
}

// Literal syntax:  v | -v | v:SET | -v:SET   with SET = item(,item)*, item = a | a-b.
// "v" means v:1, a leading '-' complements the set.
static std::pair<int, Bits> parseLit(const Solver& s, const std::string& tok) {
    size_t i = 0;
    bool neg = false;
    if (tok[i] == '-') {
        neg = true;
        i++;
    }
    int var = parseInt(tok, i);
    if (var < 1 || var > s.numVars()) throw std::runtime_error("variable out of range in literal '" + tok + "'");
    int dom = s.domain(var);
    Bits set(Bits::wordsFor(dom));
    auto add = [&](int v) {
        if (v >= dom) throw std::runtime_error("value out of domain in literal '" + tok + "'");
        set.set(v);
    };
    if (i == tok.size()) {
        add(1);
    } else {
        if (tok[i] != ':') throw std::runtime_error("bad literal '" + tok + "'");
        i++;
        for (;;) {
            int a = parseInt(tok, i), b = a;
            if (i < tok.size() && tok[i] == '-') {
                i++;
                b = parseInt(tok, i);
            }
            for (int v = a; v <= b; v++) add(v);
            if (i == tok.size()) break;
            if (tok[i] != ',') throw std::runtime_error("bad literal '" + tok + "'");
            i++;
        }
    }
    if (neg) set = Bits::full(dom).minus(set);
    return {var, set};
}

static void parse(std::istream& in, Solver& s, int& nvars, int& nclauses) {
    std::string line;
    bool header = false;
    std::vector<std::pair<int, Bits>> clause;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tok;
        if (!(ls >> tok)) continue;
        if (tok == "c") continue;
        if (tok == "p") {
            std::string fmt;
            if (header || !(ls >> fmt >> nvars >> nclauses) || (fmt != "cnf" && fmt != "dcnf") || nvars < 0)
                throw std::runtime_error("bad header line: " + line);
            s.setNumVars(nvars);
            header = true;
            continue;
        }
        if (!header) throw std::runtime_error("missing 'p dcnf' header");
        if (tok == "d") {
            int dom, x;
            if (!(ls >> dom)) throw std::runtime_error("bad domain line: " + line);
            while (ls >> x && x != 0) s.setDomain(x, dom);
            continue;
        }
        if (tok == "a" || tok == "e") {
            std::vector<int> vs;
            int x;
            while (ls >> x && x != 0) vs.push_back(x);
            s.addBlock(tok == "a" ? FORALL : EXISTS, vs);
            continue;
        }
        do {
            if (tok == "0") {
                s.addClause(clause);
                clause.clear();
            } else {
                clause.push_back(parseLit(s, tok));
            }
        } while (ls >> tok);
    }
    if (!header) throw std::runtime_error("missing 'p dcnf' header");
    if (!clause.empty()) throw std::runtime_error("last clause is not terminated by 0");
}

int main(int argc, char** argv) {
    int verbosity = 0, restart_base = 100, max_learned = 2000;
    const char* path = nullptr;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v"))
            verbosity++;
        else if (!strncmp(argv[i], "--restart-base=", 15))
            restart_base = atoi(argv[i] + 15);
        else if (!strncmp(argv[i], "--max-learned=", 14))
            max_learned = atoi(argv[i] + 14);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: discqbf [options] [file.dqdimacs]\n"
                   "  reads stdin if no file is given; exit code 10 = true, 20 = false\n"
                   "  -v                 statistics (-v -v: learned constraints, -v -v -v: decisions)\n"
                   "  --restart-base=N   conflicts per Luby restart unit (default 100)\n"
                   "  --max-learned=N    learned constraints before first reduction (default 2000)\n");
            return 0;
        } else if (!path)
            path = argv[i];
        else {
            fprintf(stderr, "error: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
    }
    Solver s;
    s.verbosity = verbosity;
    s.restart_base = restart_base > 0 ? restart_base : 1;
    s.max_learned = max_learned > 0 ? max_learned : 1;
    int nvars = 0, nclauses = 0;
    try {
        if (path) {
            std::ifstream f(path);
            if (!f) {
                fprintf(stderr, "error: cannot open '%s'\n", path);
                return 1;
            }
            parse(f, s, nvars, nclauses);
        } else {
            parse(std::cin, s, nvars, nclauses);
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "parse error: %s\n", e.what());
        return 1;
    }

    Result r;
    try {
        r = s.solve();
    } catch (const std::exception& e) {
        fprintf(stderr, "%s\n", e.what());
        return 2;
    }
    printf("s cnf %d %d %d\n", r == RESULT_TRUE ? 1 : 0, nvars, nclauses);
    if (verbosity > 0) {
        const Stats& st = s.stats;
        fprintf(stderr,
                "c decisions %lld\nc propagations %lld\nc conflicts %lld\nc solutions %lld\n"
                "c restarts %lld\nc deleted %lld\n",
                st.decisions, st.propagations, st.conflicts, st.solutions, st.restarts, st.deleted);
    }
    return r;
}
