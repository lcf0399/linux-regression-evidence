# 2026-06-23 mprotect shared-dirty bare-metal kernel queue

This directory contains a bare-metal standalone rerun. It does not use QEMU.
The runner installed each kernel on the same machine, booted into the target
kernel through a systemd/GRUB queue, and ran the same standalone reproducer.

Scenario:

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`
- 60-second boot settle before each benchmark step

Metric direction:

- `iteration_ns_per_page`: lower is better
- `protect_ns_per_page`, `restore_ns_per_page`, and `post_touch_ns_per_page`
  are phase-level metrics
- `expected_match_ratio=100` and `unexpected_results=0` mean the semantic
  check passed

The queue ran 5 kernels interleaved across 3 boot-level batches, for 15 formal
steps:

1. `6.12.77-bm-6.12.77`
2. `6.19.9-bm-6.19.9`
3. `6.19.9-bm-6.19.9-pedro-v3`
4. `7.0.9-bm-7.0.9`
5. `7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57`

Summaries:

- `aggregate-summary.csv`
- `step-summary.csv`

Current aggregate:

```text
kernel                                                        n  iteration_mean  iteration_cv_pct
6.12.77-bm-6.12.77                                           3          26.000             0.000
6.19.9-bm-6.19.9                                             3          37.000             0.000
6.19.9-bm-6.19.9-pedro-v3                                    3          39.000             0.000
7.0.9-bm-7.0.9                                               3          36.000             0.000
7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57                  3          39.000             0.000
```

Note: `logs/step-000_20260623T084217Z_6.12.77-bm-6.12.77.log` records the
initial queue-start permission failure. It has no matching `.done` step and is
not included in `step-summary.csv` or `aggregate-summary.csv`. The formal steps
start at `step-000_20260623T084831Z_6.12.77-bm-6.12.77.done`.
