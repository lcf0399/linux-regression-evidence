CC ?= gcc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror
BUILD_DIR ?= build
FULL_TARGET := $(BUILD_DIR)/io_uring_msg_ring_round
SHORT_TARGET := $(BUILD_DIR)/io_uring_msg_ring_f0_standalone
DIAG_TARGET := $(BUILD_DIR)/io_uring_msg_ring_node_cache_diag
TARGETS := $(FULL_TARGET) $(SHORT_TARGET) $(DIAG_TARGET)

.PHONY: all clean

all: $(TARGETS)

$(FULL_TARGET): io_uring_msg_ring_round.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(SHORT_TARGET): io_uring_msg_ring_f0_standalone.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(DIAG_TARGET): io_uring_msg_ring_node_cache_diag.c \
		io_uring_msg_ring_f0_standalone.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(TARGETS)
