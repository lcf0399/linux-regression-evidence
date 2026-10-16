CC = gcc-15
PKG_CONFIG ?= pkg-config
CFLAGS = -O2 -g -std=gnu11 -Wall -Wextra -Werror -pthread

.PHONY: all
all: build/control

build/control: control.c ../../../reproducer/cg_shared.h ../../../reproducer/partial_scope.h
	$(PKG_CONFIG) --exists libbpf
	mkdir -p build
	$(CC) $(CFLAGS) -I../../../reproducer control.c -o $@ $(shell $(PKG_CONFIG) --cflags --libs libbpf)
