CC ?= cc
CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Werror

.PHONY: all clean

all: io_uring_futex_waitv_wake_standalone

io_uring_futex_waitv_wake_standalone: io_uring_futex_waitv_wake_standalone.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	$(RM) io_uring_futex_waitv_wake_standalone
