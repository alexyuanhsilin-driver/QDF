// Bits: a fixed-width set of variable values (states), one bit per value.
// Every Bits object belonging to variable x has exactly nwords(x) words,
// so binary operations can assume equal sizes.
#pragma once

#include <cstdint>
#include <vector>

namespace discqbf {

class Bits {
   public:
    std::vector<uint64_t> w;

    Bits() = default;
    explicit Bits(int nwords) : w(nwords, 0) {}

    static int wordsFor(int domain) { return (domain + 63) / 64; }

    // All values 0..domain-1.
    static Bits full(int domain) {
        Bits b(wordsFor(domain));
        for (int v = 0; v < domain; v++) b.set(v);
        return b;
    }

    void set(int v) { w[v >> 6] |= (uint64_t)1 << (v & 63); }
    void clear(int v) { w[v >> 6] &= ~((uint64_t)1 << (v & 63)); }
    bool test(int v) const { return (w[v >> 6] >> (v & 63)) & 1; }

    bool empty() const {
        for (uint64_t x : w)
            if (x) return false;
        return true;
    }
    // true iff this is a subset of o
    bool subsetOf(const Bits& o) const {
        for (size_t i = 0; i < w.size(); i++)
            if (w[i] & ~o.w[i]) return false;
        return true;
    }
    bool intersects(const Bits& o) const {
        for (size_t i = 0; i < w.size(); i++)
            if (w[i] & o.w[i]) return true;
        return false;
    }
    bool operator==(const Bits& o) const { return w == o.w; }

    Bits& operator&=(const Bits& o) {
        for (size_t i = 0; i < w.size(); i++) w[i] &= o.w[i];
        return *this;
    }
    Bits& operator|=(const Bits& o) {
        for (size_t i = 0; i < w.size(); i++) w[i] |= o.w[i];
        return *this;
    }
    // this \ o
    Bits minus(const Bits& o) const {
        Bits r = *this;
        for (size_t i = 0; i < w.size(); i++) r.w[i] &= ~o.w[i];
        return r;
    }

    int count() const {
        int c = 0;
        for (uint64_t x : w) c += __builtin_popcountll(x);
        return c;
    }
    // Lowest value in the set, -1 if empty.
    int first() const {
        for (size_t i = 0; i < w.size(); i++)
            if (w[i]) return (int)(i * 64) + __builtin_ctzll(w[i]);
        return -1;
    }

    // Calls f(v) for every value v in the set, in increasing order.
    template <typename F>
    void forEach(F f) const {
        for (size_t i = 0; i < w.size(); i++) {
            uint64_t x = w[i];
            while (x) {
                f((int)(i * 64) + __builtin_ctzll(x));
                x &= x - 1;
            }
        }
    }
};

}  // namespace discqbf
