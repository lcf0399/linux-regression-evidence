CC ?= cc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror

BUILD_DIR := build
STANDALONE := $(BUILD_DIR)/io_uring_futex_wait_wake_standalone
FORMAL := $(BUILD_DIR)/io_uring_futex_round
TARGETS := $(STANDALONE) $(FORMAL)

.PHONY: all clean

all: $(TARGETS)

$(STANDALONE): io_uring_futex_wait_wake_standalone.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(FORMAL): io_uring_futex_round.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
