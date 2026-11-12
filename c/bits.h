/* Value sets of discrete variables as arrays of 64-bit words.
   All sets of a variable x have exactly nw = bits_words(domain(x)) words. */
#ifndef DISCQBF_BITS_H
#define DISCQBF_BITS_H

#include <stdint.h>
#include <string.h>

typedef uint64_t word;

static inline int bits_words (int domain) { return (domain + 63) / 64; }

static inline void bits_set (word * b, int v) { b[v >> 6] |= (word) 1 << (v & 63); }
static inline int bits_test (const word * b, int v) { return (b[v >> 6] >> (v & 63)) & 1; }

static inline void bits_clear_all (word * b, int nw) { memset (b, 0, nw * sizeof (word)); }
static inline void bits_copy (word * dst, const word * src, int nw) { memcpy (dst, src, nw * sizeof (word)); }

/* b := {0, ..., domain-1} */
static inline void
bits_fill (word * b, int domain)
{
  int nw = bits_words (domain), v;
  bits_clear_all (b, nw);
  for (v = 0; v < domain; v++)
    bits_set (b, v);
}

static inline int
bits_empty (const word * b, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    if (b[i])
      return 0;
  return 1;
}

/* a is a subset of b */
static inline int
bits_subset (const word * a, const word * b, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    if (a[i] & ~b[i])
      return 0;
  return 1;
}

static inline int
bits_intersects (const word * a, const word * b, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    if (a[i] & b[i])
      return 1;
  return 0;
}

static inline int
bits_equal (const word * a, const word * b, int nw)
{
  return !memcmp (a, b, nw * sizeof (word));
}

static inline void
bits_and (word * dst, const word * src, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    dst[i] &= src[i];
}

static inline void
bits_or (word * dst, const word * src, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    dst[i] |= src[i];
}

/* dst := a \ b */
static inline void
bits_minus (word * dst, const word * a, const word * b, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    dst[i] = a[i] & ~b[i];
}

/* b == {0, ..., domain-1} */
static inline int
bits_is_full (const word * b, int domain)
{
  int v;
  for (v = 0; v < domain; v++)
    if (!bits_test (b, v))
      return 0;
  return 1;
}

static inline int
bits_count (const word * b, int nw)
{
  int i, c = 0;
  for (i = 0; i < nw; i++)
    c += __builtin_popcountll (b[i]);
  return c;
}

/* Lowest value in b, or -1 if b is empty. */
static inline int
bits_first (const word * b, int nw)
{
  int i;
  for (i = 0; i < nw; i++)
    if (b[i])
      return i * 64 + __builtin_ctzll (b[i]);
  return -1;
}

/* Iterates v over all values in b in increasing order. Do not use 'break'
   inside the body (it only leaves the inner loop). */
#define BITS_FOREACH(b, nw, v)                                          \
  for (int bits_i_ = 0; bits_i_ < (nw); bits_i_++)                      \
    for (word bits_x_ = (b)[bits_i_];                                   \
         bits_x_ && (((v) = bits_i_ * 64 + __builtin_ctzll (bits_x_)), 1); \
         bits_x_ &= bits_x_ - 1)

#endif
