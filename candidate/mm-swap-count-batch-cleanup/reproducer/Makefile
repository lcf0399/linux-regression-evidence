CC ?= cc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror

.PHONY: all check
all: build/shared_probe

build/shared_probe: shared_probe.c
	mkdir -p build
	$(CC) $(CFLAGS) $< -o $@

check: build/shared_probe
	./build/shared_probe --selftest
	python3 -B verify_results.py
