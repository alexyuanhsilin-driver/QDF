#!/bin/bash
# Full test suite. Optionally pass the path to an original DepQBF binary to
# also cross-check Boolean instances:  ./run_tests.sh /path/to/depqbf
set -e
cd "$(dirname "$0")"

echo "== hand-written cases"
python3 run_cases.py

echo "== random formulas vs. brute force (C and C++ must also report identical statistics)"
python3 fuzz.py -n 1000 --seed 1 --vars 8 --dom 6
python3 fuzz.py -n 500 --seed 2 --vars 14 --dom 3
python3 fuzz.py -n 300 --seed 3 --vars 8 --dom 70     # value sets wider than 64 bits

echo "== stress: restart after every conflict, keep only a few learned constraints"
python3 fuzz.py -n 500 --seed 4 --vars 12 --dom 4 --stress

echo "== larger formulas, C vs. C++ only"
python3 fuzz.py -n 100 --seed 5 --vars 120 --dom 3 --no-reference --timeout 60

if [ -n "$1" ]; then
  echo "== Boolean QBFs vs. DepQBF ($1)"
  python3 vs_depqbf.py "$1" -n 300 --vars 150
  echo "== discrete QBFs vs. DepQBF on their Boolean encoding"
  python3 vs_depqbf_encoded.py "$1" --cases
  python3 vs_depqbf_encoded.py "$1" -n 300 --vars 8 --dom 6 --reference
  python3 vs_depqbf_encoded.py "$1" -n 200 --vars 60 --dom 5 --clauses 2
fi
echo "all tests passed"
