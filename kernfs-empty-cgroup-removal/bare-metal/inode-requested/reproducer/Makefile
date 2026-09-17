CC = gcc-15
PKG_CONFIG ?= pkg-config
CFLAGS = -O2 -g -std=gnu11 -Wall -Wextra -Werror -pthread
SHARED = ../../../reproducer
CYCLE = ../../inode-inited/reproducer/control.c

.PHONY: all
all: build/original build/cycle build/lookup build/first-creation

build:
	mkdir -p $@

build/original: $(SHARED)/control.c $(SHARED)/cg_shared.h $(SHARED)/partial_scope.h | build
	$(PKG_CONFIG) --exists libbpf
	$(CC) $(CFLAGS) -I$(SHARED) $< -o $@ $(shell $(PKG_CONFIG) --cflags --libs libbpf)

build/cycle: $(CYCLE) $(SHARED)/cg_shared.h $(SHARED)/partial_scope.h | build
	$(PKG_CONFIG) --exists libbpf
	$(CC) $(CFLAGS) -I$(SHARED) $< -o $@ $(shell $(PKG_CONFIG) --cflags --libs libbpf)

build/lookup: lookup.c | build
	$(CC) $(CFLAGS) $< -o $@

build/first-creation: first-creation.c | build
	$(CC) $(CFLAGS) $< -o $@
