CC ?= gcc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wformat=2 -Wshadow
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/io_uring_nop_round

.PHONY: all clean

all: $(TARGET)

$(TARGET): io_uring_nop_round.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
