/* Command line front end and DQDIMACS parser (twin of ../cpp/main.cpp).
   See ../README.md for the format. */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solver.h"

static char errbuf[512];

#define FAIL(...)                                               \
  do {                                                          \
    snprintf (errbuf, sizeof errbuf, __VA_ARGS__);              \
    return errbuf;                                              \
  } while (0)

static const char *
parse_int (const char *tok, const char **p, int *out)
{
  long long v = 0;
  if (!isdigit ((unsigned char) **p))
    FAIL ("expected a number in '%s'", tok);
  while (isdigit ((unsigned char) **p))
    {
      v = v * 10 + (*(*p)++ - '0');
      if (v > 1000000000)
        FAIL ("number too large in '%s'", tok);
    }
  *out = (int) v;
  return 0;
}

/* Literal syntax:  v | -v | v:SET | -v:SET   with SET = item(,item)*, item = a | a-b.
   "v" means v:1, a leading '-' complements the set. 'set' must have room
   for the variable's words. */
static const char *
parse_lit (Solver * s, const char *tok, int *var, word * set)
{
  const char *p = tok, *err;
  int neg = 0, dom, a, b, v;
  if (*p == '-')
    {
      neg = 1;
      p++;
    }
  if ((err = parse_int (tok, &p, var)))
    return err;
  if (*var < 1 || *var > solver_num_vars (s))
    FAIL ("variable out of range in literal '%s'", tok);
  dom = solver_domain (s, *var);
  bits_clear_all (set, bits_words (dom));
  if (!*p)
    {
      if (1 >= dom)
        FAIL ("value out of domain in literal '%s'", tok);
      bits_set (set, 1);
    }
  else
    {
      if (*p != ':')
        FAIL ("bad literal '%s'", tok);
      p++;
      for (;;)
        {
          if ((err = parse_int (tok, &p, &a)))
            return err;
          b = a;
          if (*p == '-')
            {
              p++;
              if ((err = parse_int (tok, &p, &b)))
                return err;
            }
          for (v = a; v <= b; v++)
            {
              if (v >= dom)
                FAIL ("value out of domain in literal '%s'", tok);
              bits_set (set, v);
            }
          if (!*p)
            break;
          if (*p != ',')
            FAIL ("bad literal '%s'", tok);
          p++;
        }
    }
  if (neg)
    for (v = 0; v < dom; v++)
      {
        if (bits_test (set, v))
          set[v >> 6] &= ~((word) 1 << (v & 63));
        else
          bits_set (set, v);
      }
  return 0;
}

/* Strict integer token (whole token must be a number, optionally negative). */
static int
read_int_token (const char *tok, int *out)
{
  char *end;
  long v = strtol (tok, &end, 10);
  if (!*tok || *end)
    return 0;
  *out = (int) v;
  return 1;
}

static const char *
parse (FILE * in, Solver * s, int *nvars, int *nclauses)
{
  char *line = 0, *tok, *save;
  size_t cap = 0;
  int header = 0, n = 0, ncap = 0, *cvars = 0, *ivars = 0, icap = 0, x;
  word **csets = 0;
  const char *err = 0;

  while (!err && getline (&line, &cap, in) != -1)
    {
      if (!(tok = strtok_r (line, " \t\r\n", &save)))
        continue;
      if (!strcmp (tok, "c"))
        continue;
      if (!strcmp (tok, "p"))
        {
          char *fmt = strtok_r (0, " \t\r\n", &save);
          char *a = strtok_r (0, " \t\r\n", &save);
          char *b = strtok_r (0, " \t\r\n", &save);
          if (header || !fmt || !a || !b || (strcmp (fmt, "cnf") && strcmp (fmt, "dcnf"))
              || !read_int_token (a, nvars) || !read_int_token (b, nclauses) || *nvars < 0)
            {
              snprintf (errbuf, sizeof errbuf, "bad header line");
              err = errbuf;
              break;
            }
          solver_set_num_vars (s, *nvars);
          header = 1;
          continue;
        }
      if (!header)
        {
          err = "missing 'p dcnf' header";
          break;
        }
      if (!strcmp (tok, "d"))
        {
          int dom;
          char *t = strtok_r (0, " \t\r\n", &save);
          if (!t || !read_int_token (t, &dom))
            {
              err = "bad domain line";
              break;
            }
          while (!err && (t = strtok_r (0, " \t\r\n", &save)) && read_int_token (t, &x) && x != 0)
            err = solver_set_domain (s, x, dom);
          continue;
        }
      if (!strcmp (tok, "a") || !strcmp (tok, "e"))
        {
          int k = 0;
          char *t;
          QType q = !strcmp (tok, "a") ? QTYPE_FORALL : QTYPE_EXISTS;
          while ((t = strtok_r (0, " \t\r\n", &save)) && read_int_token (t, &x) && x != 0)
            {
              if (k == icap)
                {
                  icap = icap ? 2 * icap : 16;
                  ivars = realloc (ivars, icap * sizeof (int));
                }
              ivars[k++] = x;
            }
          err = solver_add_block (s, q, ivars, k);
          continue;
        }
      do
        {
          if (!strcmp (tok, "0"))
            {
              solver_add_clause (s, cvars, csets, n);
              for (x = 0; x < n; x++)
                free (csets[x]);
              n = 0;
            }
          else
            {
              int var;
              word *set;
              const char *p = tok + (*tok == '-');
              /* Peek the variable to size the set before parsing the literal. */
              if (!isdigit ((unsigned char) *p) || (var = atoi (p)) < 1 || var > solver_num_vars (s))
                {
                  snprintf (errbuf, sizeof errbuf, "variable out of range in literal '%s'", tok);
                  err = errbuf;
                  break;
                }
              set = calloc (bits_words (solver_domain (s, var)), sizeof (word));
              if ((err = parse_lit (s, tok, &var, set)))
                {
                  free (set);
                  break;
                }
              if (n == ncap)
                {
                  ncap = ncap ? 2 * ncap : 16;
                  cvars = realloc (cvars, ncap * sizeof (int));
                  csets = realloc (csets, ncap * sizeof (word *));
                }
              cvars[n] = var;
              csets[n] = set;
              n++;
            }
        }
      while ((tok = strtok_r (0, " \t\r\n", &save)));
    }
  if (!err && !header)
    err = "missing 'p dcnf' header";
  if (!err && n)
    err = "last clause is not terminated by 0";
  for (x = 0; x < n; x++)
    free (csets[x]);
  free (cvars);
  free (csets);
  free (ivars);
  free (line);
  return err;
}

int
main (int argc, char **argv)
{
  int verbosity = 0, nvars = 0, nclauses = 0, i, restart_base = 100, max_learned = 2000;
  const char *path = 0, *err;
  FILE *in = stdin;
  Solver *s;
  Result r;
  for (i = 1; i < argc; i++)
    {
      if (!strcmp (argv[i], "-v"))
        verbosity++;
      else if (!strncmp (argv[i], "--restart-base=", 15))
        restart_base = atoi (argv[i] + 15);
      else if (!strncmp (argv[i], "--max-learned=", 14))
        max_learned = atoi (argv[i] + 14);
      else if (!strcmp (argv[i], "-h") || !strcmp (argv[i], "--help"))
        {
          printf ("usage: discqbf [options] [file.dqdimacs]\n"
                  "  reads stdin if no file is given; exit code 10 = true, 20 = false\n"
                  "  -v                 statistics (-v -v: learned constraints, -v -v -v: decisions)\n"
                  "  --restart-base=N   conflicts per Luby restart unit (default 100)\n"
                  "  --max-learned=N    learned constraints before first reduction (default 2000)\n");
          return 0;
        }
      else if (!path)
        path = argv[i];
      else
        {
          fprintf (stderr, "error: unexpected argument '%s'\n", argv[i]);
          return 1;
        }
    }
  if (path && !(in = fopen (path, "r")))
    {
      fprintf (stderr, "error: cannot open '%s'\n", path);
      return 1;
    }
  s = solver_new ();
  solver_set_verbosity (s, verbosity);
  solver_set_limits (s, restart_base, max_learned);
  err = parse (in, s, &nvars, &nclauses);
  if (path)
    fclose (in);
  if (err)
    {
      fprintf (stderr, "parse error: %s\n", err);
      solver_delete (s);
      return 1;
    }
  r = solver_solve (s);
  printf ("s cnf %d %d %d\n", r == RESULT_TRUE ? 1 : 0, nvars, nclauses);
  if (verbosity > 0)
    {
      const Stats *st = solver_stats (s);
      fprintf (stderr,
               "c decisions %lld\nc propagations %lld\nc conflicts %lld\nc solutions %lld\n"
               "c restarts %lld\nc deleted %lld\n",
               st->decisions, st->propagations, st->conflicts, st->solutions, st->restarts, st->deleted);
    }
  solver_delete (s);
  return r;
}
