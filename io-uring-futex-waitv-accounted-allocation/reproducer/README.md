# Reproducer

`io_uring_futex_waitv_wake_standalone.c` is the recommended review source. It
is a 424-line raw-UAPI program with no liburing dependency. Build it with:

```sh
make
```

Run it on a kernel that supports io_uring futex operations:

```sh
./io_uring_futex_waitv_wake_standalone
```

It pins itself to CPU 2, performs three warm-up rounds followed by 15 measured
rounds, and prints a TSV with 4,096 WAITV/wake pairs per round. Any CQE,
returned-value, affinity, overflow, or residual-waiter mismatch exits with an
error.

`io_uring_futex_round.c` is the unchanged formal multi-profile source used by
the exact run. The command for this case is:

```sh
cc -O2 -g -std=gnu11 -Wall -Wextra -Werror \
  -o io_uring_futex_round io_uring_futex_round.c
./io_uring_futex_round --profile v0_waitv_wake --mode point \
  --output result.tsv --execute
```

The standalone SHA-256 is
`985f8acf23001bf3ef30c2ea3b781fe054e53c34b24b336426b4f26f52129408`.
The result comment at the top records the earlier exact run that motivated
this independent rerun. The source is kept byte-for-byte identical to the
tested input; the current rerun values are in [`../bare-metal/`](../bare-metal/).
