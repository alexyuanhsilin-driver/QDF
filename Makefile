.PHONY: all debug test test-quick clean

all:
	$(MAKE) -C c
	$(MAKE) -C cpp

# Builds with assertions and address/undefined-behaviour sanitizers.
debug:
	gcc -std=c99 -pedantic -Wall -Wextra -g -O1 -fsanitize=address,undefined c/main.c c/solver.c -o c/discqbf-debug
	clang++ -std=c++20 -Wall -Wextra -g -O1 -fsanitize=address,undefined cpp/main.cpp cpp/Solver.cpp -o cpp/discqbf-debug

test-quick: all
	cd tests && python3 run_cases.py
	cd tests && python3 fuzz.py -n 300

test: all
	./tests/run_tests.sh

clean:
	$(MAKE) -C c clean
	$(MAKE) -C cpp clean
	rm -f c/discqbf-debug cpp/discqbf-debug
	rm -rf tests/failures* tests/__pycache__
