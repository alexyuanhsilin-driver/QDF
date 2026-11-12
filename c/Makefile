CC = gcc
CFLAGS ?= -std=c99 -pedantic -Wall -Wextra -O3 -DNDEBUG
# debug build: make CFLAGS="-std=c99 -g -O1 -fsanitize=address,undefined"

discqbf: main.c solver.c solver.h bits.h
	$(CC) $(CFLAGS) main.c solver.c -o $@

clean:
	rm -f discqbf
