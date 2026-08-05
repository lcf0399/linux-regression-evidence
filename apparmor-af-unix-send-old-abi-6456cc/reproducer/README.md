# Reproducers

This directory contains both sources used for the reported measurements:

| source | purpose | reported profiles |
| --- | --- | --- |
| `af_unix_sendmmsg_standalone.c` | concise dependency-free reproducer | direct `sendmmsg()` |
| `io_uring_net_round.c` | unmodified original formal workload | `u0` direct `sendmmsg()` and `s0` io_uring `IORING_OP_SEND` |

## Concise standalone reproducer

`af_unix_sendmmsg_standalone.c` is a 328-line, dependency-free reproducer for
the primary direct-system-call result. It uses an AF_UNIX datagram socketpair,
prepares 32 distinct 128-byte datagrams, times one `sendmmsg()` call, then
drains and validates all messages outside timing.

Build and run:

```bash
make
./af_unix_sendmmsg_standalone
```

Defaults match the reported standalone run: CPU 2, 3 warm-up rounds, 15
measured rounds, and 65,536 messages per round. The optional `--cpu`,
`--warmups`, `--rounds`, and `--messages` arguments change those values;
`--messages` must be a multiple of 32.

Each output row includes messages, batches, elapsed nanoseconds,
ns/message, and a semantic-pass flag. The program also records the kernel
release, boot ID, and `/proc/self/attr/current`. All payload, length, count,
and empty-queue checks are mandatory.

This source independently reproduces the direct `sendmmsg()` result without
initializing io_uring.

## Original formal workload

`io_uring_net_round.c` is the unmodified 1,130-line source used by the larger
formal runner. Its SHA-256 is
`e1b78e860713b4abc9e0ada7bafc9794172a304af9bc67a0d1db1c3d491b2216`.
It uses the raw io_uring UAPI and has no `liburing` dependency.

The two profiles used for the AppArmor exact-source-delta table can be run as:

```bash
./io_uring_net_round --profile u0 --mode smoke --execute
./io_uring_net_round --profile s0 --mode smoke --execute

./io_uring_net_round --profile u0 --mode point --execute --output u0.tsv
./io_uring_net_round --profile s0 --mode point --execute --output s0.tsv
```

`u0` times batched `sendmmsg()` on the same AF_UNIX datagram socketpair.
`s0` times `IORING_OP_SEND` with the same queue depth and payload size. The
program fixes CPU 2, performs 3 warm-up rounds and 15 measured rounds, and
keeps peer draining and payload validation outside the timed region. It
refuses to overwrite an existing output file.

The source also retains the other original profiles (`s1`, `r0`, `r1`, `m0`,
and `c0`) for provenance, but they were not used for the exact AppArmor table.
Its `describe` output names the original v6.12.95/v7.1.3 release-screen
contract; measured rows record the kernel actually running.

The standalone and formal percentages are independent replications. They use
different harnesses and matched but different runtime preemption modes, so
their values must not be pooled or subtracted from one another.
