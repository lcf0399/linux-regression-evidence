# Reproducer

`io_uring_memmap_round.c` is the **exact 1,081-line raw-UAPI workload used to
produce the formal numbers** in this bundle. Its SHA-256 is
`fc8135fcb64de09c5396437608dabee9a54b01d5c0a649720728ea3f29250127`.
It retains the complete output schema, semantic checks, and additional control
profiles; the present regression claim uses only `l0_standard_lifecycle`.

Build it with:

```bash
make
```

Inspect the fixed shape without making syscalls:

```bash
./run_memmap.sh --profile l0_standard_lifecycle --phase describe
```

Run a small semantic check:

```bash
MEMMAP_EXECUTE_ACK=YES \
  ./run_memmap.sh --profile l0_standard_lifecycle --phase smoke \
  --out-dir /tmp/io-memmap-smoke
```

Run one point with the formal 3-warm-up/15-measured shape:

```bash
MEMMAP_EXECUTE_ACK=YES MEMMAP_TIMING_ACK=YES \
MEMMAP_REQUIRED_REQUESTED_PREEMPT=none \
MEMMAP_REQUIRED_BUILD_PREEMPT=dynamic \
MEMMAP_REQUIRED_ACTUAL_PREEMPT=full \
  ./run_memmap.sh --profile l0_standard_lifecycle --phase point \
  --out-dir /tmp/io-memmap-point
```

The runner checks actual preemption, CPU 2 governor/EPP, Turbo, and tracing
state, then invokes the validator. One run does not replace the formal fresh-
boot parent/child/parent comparison, which also requires matched configs,
compiler, Kbuild metadata, workload binary, and machine policy.

No separate concise source is introduced here, avoiding any implication that
an independently unvalidated rewrite produced the formal result.
