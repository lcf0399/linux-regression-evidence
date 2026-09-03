# Reproducers

## Exact experiment source

`io_uring_msg_ring_round.c` is the exact 1,165-line source used for the
reported runs;
its SHA-256 is
`21c99cd5cff34c21f815e7427e6f64997df864a6fb84f8c0cd26ede3d720e03c`.
The source contains three profiles, but this evidence uses only
`--profile f0`.

F0 registers one 4 KiB memfd in source fixed-file slot 0. Each round creates
a target ring with a sparse 4,096-slot fixed-file table and uses
`IORING_OP_MSG_RING` with `IORING_MSG_SEND_FD` to fill the empty slots in
batches of 64. The timed region covers the 4,096 SEND_FD operations and their
source and target completions. Reading every installed slot and checking its
sentinel byte happen after timing.

Build and run a semantic smoke check:

```sh
./run_f0_once.sh smoke /tmp/msg-ring-f0-smoke
```

Run one full point (3 untimed warm-up rounds and 15 measured rounds):

```sh
ulimit -n 65536
./run_f0_once.sh point /tmp/msg-ring-f0-point
```

The program pins itself to logical CPU 2. It does not configure the CPU
governor, EPP, Turbo, preemption mode, or reboot the machine; those controls
must be applied and recorded by the experiment harness before comparing
kernels. Some container seccomp policies deny `io_uring_setup(2)`, so use a
host or VM where io_uring is enabled.

The primary metric is end-to-end nanoseconds per successful fixed-file
installation. It includes userspace SQE preparation, `io_uring_enter()`, and
completion processing, but excludes target-table creation, full slot
verification, and teardown.

## Short F0-only source

`io_uring_msg_ring_f0_standalone.c` is a 422-line, commented review version
with SHA-256
`064d26197b1a9cc5e14bb7d648255c3403becb0d4278703b3f8b035a2398d1fe`.
It keeps only the F0 path and uses raw io_uring UAPI without liburing. It
retains the source/target CQE, unique-slot, full sentinel-read, affinity,
drop, overflow, and outstanding-operation checks.

```sh
make
./build/io_uring_msg_ring_f0_standalone --smoke
./build/io_uring_msg_ring_f0_standalone
```

An independent exact-kernel cross-check found the child `11.145%` slower than
the parent midpoint, consistent with the full source's `11.621%`. The short
run passed all semantic checks, but its parent controls drifted by `2.645%`,
above the formal `2%` gate. It is therefore a readability and directional
cross-check, not a replacement for the exact experiment source or result.

The slot-count scope check rebuilt the exact source with only
`FD_OPS_PER_ROUND` changed to 64, 256, 1,024, or 4,096. It kept the queue
depth at 64 and used the same exact-kernel sequence and semantic gates. The
unchanged 4,096-operation source remains the primary reproducer.

## Node-cache cold/warm diagnostic

`io_uring_msg_ring_node_cache_diag.c` is the 267-line source for the 128-slot
cold/warm diagnostic. Its SHA-256 is
`e6d41ddcb2d1109bea5e472e7d600305f56ffe368239ec7d40c2e76d4f8c2561`.
It includes the short standalone source above so that it reuses the same raw
io_uring helpers and semantic checks. The 128-slot size matches
`IO_ALLOC_CACHE_MAX` in v7.1.3; it does not replace the original 4,096-slot
exact-commit result.

```sh
make
./build/io_uring_msg_ring_node_cache_diag --smoke
./build/io_uring_msg_ring_node_cache_diag --timing \
  --slots 128 --warmups 1 --pairs 15 --replicates 128
```

`trace_node_cache.sh` collects the separate PID-filtered function trace used
to infer cache hits. It requires root access to tracefs and must not be active
during clean timing:

```sh
./trace_node_cache.sh ./build/io_uring_msg_ring_node_cache_diag cold /tmp/msg-ring-cold
./trace_node_cache.sh ./build/io_uring_msg_ring_node_cache_diag warm /tmp/msg-ring-warm
```

## Prefill diagnostic patches

[`0001-diagnostic-prefill-sparse-node-cache.patch`](0001-diagnostic-prefill-sparse-node-cache.patch)
is the exact one-file source delta used for the 4,096-node registration-prefill
diagnostic. It applies to `7029acd8a950` and has SHA-256
`363f914fb8ded99415232348b51820d9a0030a0dac72dd3a4e99035a33dd4c23`.

The patch is intentionally tied to the 4,096-slot F0 shape. It moves raw node
allocation and zeroing to sparse-table registration but retains the later
logical allocation, initialization, and installation path. It is published
for reproducibility only; it is not a fix proposal and does not measure the
registration-time or retained-memory cost that it introduces.

[`0002-diagnostic-prime-node-slab-backing.patch`](0002-diagnostic-prime-node-slab-backing.patch)
is the narrower slab-backing-only delta. It applies to `7029acd8a950`, changes
`io_uring/rsrc.c` and `io_uring/rsrc.h`, and has SHA-256
`863de9cb3530bd7e71ca8dd47a2a3128f831158a1b9c065927f58646fc8ae349`.
It retains all 4,096 timed per-object allocations while moving fresh slab-page
creation to registration. It also adds registration work and retained anchors,
so it is a mechanism diagnostic rather than a fix proposal.
