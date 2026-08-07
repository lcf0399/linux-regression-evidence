# Reproducers

Two raw-UAPI sources are retained for different purposes:

- `io_uring_cancel_round.c` is the **exact 1,341-line source used for the
  reported measurements**. It preserves the full experiment contract, output
  schema, semantic checks, and additional profiles.
- `io_uring_cancel_miss_standalone.c` is a shorter, commented reproducer for
  maintainer review. It keeps only the primary `A0_MISS` path and performs its
  own setup, timing, cleanup, and semantic checks.

The concise source is an independently checked representation of the same
core operation, not the binary from which the published table was produced.
Both sources avoid a liburing dependency and default to logical CPU 2. The two
relevant profiles are:

- `a0_miss`: cancel keys that are guaranteed not to exist and require
  `-ENOENT`;
- `a0_hit`: cancel real pending poll keys and require successful cancellation.

Build both versions and run the concise reproducer:

```bash
make
./build/io_uring_cancel_miss_standalone
```

The concise version uses the formal shape: 3 warm-up rounds, 15 measured
rounds, 64 batches per round, 32 attempts per batch, and CPU 2. Its output is
an independent cross-check; the formal table remains tied to the exact source.
In a separate fresh-boot parent/child/parent run it reproduced a `+9.099%`
child slowdown. See
[`standalone-cross-check.tsv`](../bare-metal/standalone-cross-check.tsv).

Run a semantic smoke check with the exact source:

```bash
./run_a0_once.sh a0_miss smoke /tmp/io-cancel-smoke
./run_a0_once.sh a0_hit smoke /tmp/io-cancel-smoke-hit
```

Run one exact 3-warm-up/15-measured point:

```bash
./run_a0_once.sh a0_miss point /tmp/io-cancel-point
```

The runner refuses to overwrite an existing result, validates all output rows,
and records a small environment file. Formal cross-kernel comparison still
requires fresh boots, identical kernel configs and binaries, matched actual
preemption, fixed governor/EPP/Turbo settings, and a parent/child/parent order;
the standalone runner does not automate those controls.
