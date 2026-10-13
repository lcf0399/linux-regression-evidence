CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror
LDLIBS ?= -pthread

.PHONY: all selftest
all: build/whole_probe

build:
	mkdir -p $@

build/whole_probe: whole_probe.c base_probe.c | build
	$(CC) $(CFLAGS) whole_probe.c -o $@ $(LDLIBS)

selftest: build/whole_probe
	./build/whole_probe --selftest
