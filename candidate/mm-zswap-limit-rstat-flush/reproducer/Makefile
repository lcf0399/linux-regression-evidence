CC = gcc
CFLAGS = -O2 -g -std=gnu11 -Wall -Wextra -Werror

.PHONY: all selftest verify
all: build/zswap_case build/zswap_case_unlimited1

build/zswap_case: zswap_case.c peak_state.c
	mkdir -p build
	$(CC) $(CFLAGS) zswap_case.c -o $@

build/zswap_case_unlimited1: zswap_case_unlimited1.c peak_state.c
	mkdir -p build
	$(CC) $(CFLAGS) zswap_case_unlimited1.c -o $@
selftest: all
	./build/zswap_case --selftest
	./build/zswap_case_unlimited1 --selftest

verify:
	python3 verify_results.py
