CC ?= cc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror

BUILD_DIR := build
EXACT_TARGET := $(BUILD_DIR)/io_uring_cancel_round
SHORT_TARGET := $(BUILD_DIR)/io_uring_cancel_miss_standalone

.PHONY: all clean

all: $(EXACT_TARGET) $(SHORT_TARGET)

$(EXACT_TARGET): io_uring_cancel_round.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(SHORT_TARGET): io_uring_cancel_miss_standalone.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
