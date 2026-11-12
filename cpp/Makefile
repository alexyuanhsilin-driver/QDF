CXX = clang++
CXXFLAGS ?= -std=c++20 -O3 -DNDEBUG -Wall -Wextra
# debug build: make CXXFLAGS="-std=c++20 -g -O1 -fsanitize=address,undefined"

discqbf: main.cpp Solver.cpp Solver.hpp Bits.hpp
	$(CXX) $(CXXFLAGS) main.cpp Solver.cpp -o $@

clean:
	rm -f discqbf
