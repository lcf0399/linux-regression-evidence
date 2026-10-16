CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra -Werror -std=gnu11

.PHONY: all clean

all: af_unix_sendmmsg_standalone io_uring_net_round

af_unix_sendmmsg_standalone: af_unix_sendmmsg_standalone.c
	$(CC) $(CFLAGS) -o $@ $<

io_uring_net_round: io_uring_net_round.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f af_unix_sendmmsg_standalone io_uring_net_round
