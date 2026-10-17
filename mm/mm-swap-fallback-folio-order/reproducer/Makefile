CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror

.PHONY: all check clean
all: build/remaining

build/remaining: remaining.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

check: build/remaining
	./build/remaining --selftest
	python3 -B verify_results.py
	python3 -B -m unittest -v test_tools.py

clean:
	rm -f build/remaining
