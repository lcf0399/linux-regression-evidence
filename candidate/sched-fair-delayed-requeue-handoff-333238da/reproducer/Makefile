CC = gcc
CFLAGS = -O2 -g -std=gnu11 -Wall -Wextra -Werror

.PHONY: all clean
all: weighted_payload

weighted_payload: weighted_payload.c
	$(CC) $(CFLAGS) -pthread $< -o $@

clean:
	rm -f weighted_payload
