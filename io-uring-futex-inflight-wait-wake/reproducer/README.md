# Reproducer

`io_uring_futex_round.c` is the unchanged 1,112-line source used for the
formal exact A/B. `io_uring_futex_wait_wake_standalone.c` is a 338-line,
scalar-only review version that independently reproduced the same direction
and magnitude.

Both sources use the raw io_uring UAPI and require no liburing dependency.

```sh
make
./build/io_uring_futex_wait_wake_standalone --smoke
./build/io_uring_futex_wait_wake_standalone
```

The normal standalone run performs three warm-up rounds and 15 measured
rounds. Each measured round contains 512 cycles of 32 wait/wake pairs. Ring
creation, memory allocation, CPU pinning, and warm-up are outside timing.

The program exits nonzero on any unexpected CQE, result, flag, timeout,
duplicate, missing completion, or CPU migration. Some containers and security
policies disable `io_uring_setup()`; use a host kernel that permits io_uring.
