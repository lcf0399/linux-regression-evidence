# Reproducer

`io_uring_futex_round.c` is the unchanged 1,112-line source used for the
formal exact A/B. `io_uring_futex_wait_wake_standalone.c` is the compact
scalar review version. Its original 338-line private-only revision
independently reproduced the same direction and magnitude. The current
365-line revision keeps that mode as the default and adds the `--shared` mode
used for patch-2 validation.

Both sources use the raw io_uring UAPI and require no liburing dependency.

```sh
make
./build/io_uring_futex_wait_wake_standalone --smoke
./build/io_uring_futex_wait_wake_standalone
./build/io_uring_futex_wait_wake_standalone --shared --smoke
./build/io_uring_futex_wait_wake_standalone --shared
```

The normal standalone run performs three warm-up rounds and 15 measured
rounds. Each measured round contains 512 cycles of 32 wait/wake pairs. Ring
creation, memory allocation, CPU pinning, and warm-up are outside timing.
The default remains the original private-futex mode. `--shared` changes only
the futex key shape: words use a shared anonymous mapping and the SQEs omit
`FUTEX2_PRIVATE`.

Exact source and measured-binary identities for the original and patch
validation runs are in
[`../bare-metal/workload-identity.tsv`](../bare-metal/workload-identity.tsv).

The program exits nonzero on any unexpected CQE, result, flag, timeout,
duplicate, missing completion, or CPU migration. Some containers and security
policies disable `io_uring_setup()`; use a host kernel that permits io_uring.
