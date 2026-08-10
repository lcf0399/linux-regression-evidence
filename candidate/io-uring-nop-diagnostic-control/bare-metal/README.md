# Bare-metal evidence

- [`release-summary.tsv`](release-summary.tsv): the v6.12.95 versus v7.1.3
  sandwich;
- [`exact-summary.tsv`](exact-summary.tsv): the exact
  `aa00f67adc2c -> a85f31052bce` parent/child comparison;
- `*-run-selection.tsv`: kernel release, boot ID, requested/build/actual
  preemption modes, and workload hashes for every point;
- [`direct-hit.tsv`](direct-hit.tsv): 4,096 hits each on `io_nop_prep()` and
  `io_nop()` for both plain and inject profiles.

The formal workload used CPU 2, ring entries 128, QD64, 262,144 NOPs per
profile per round, three warm-up rounds, and 15 measured rounds. Tracing was
kept separate from formal timing.
