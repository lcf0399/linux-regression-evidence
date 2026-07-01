# 2026-06-23 mprotect release-window narrowing

This result set was collected on the new bare-metal node, without QEMU.  The
systemd queue booted each kernel and ran the same
`mprotect_shared_dirty_reproducer` standalone workload.

Scenario:

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`
- 60 seconds of boot settle time before each benchmark step

Primary metric:

- `iteration_ns_per_page`, lower is better
- `expected_match_ratio=100` and `unexpected_results=0` mean the semantic check passed

The queue ran 5 kernels in 3 interleaved rounds, for 15 formal steps:

1. `6.16.0-bm-6.16`
2. `6.17.0-bm-6.17`
3. `6.18.0-bm-6.18`
4. `6.18.19-bm-6.18.19`
5. `6.19.9-bm-6.19.9`

Summary files:

- `aggregate-summary.csv`
- `step-summary.csv`

Current aggregate:

```text
kernel                 n  iteration_mean  iteration_cv_pct  values
6.16.0-bm-6.16        3          25.000             0.000  25 25 25
6.17.0-bm-6.17        3          37.000             0.000  37 37 37
6.18.0-bm-6.18        3          38.000             0.000  38 38 38
6.18.19-bm-6.18.19    3          38.000             0.000  38 38 38
6.19.9-bm-6.19.9      3          36.667             1.286  37 36 37
```

All steps reported `expected_match_ratio=100` and `unexpected_results=0`.

Interpretation:

- `6.16` remains in the fast range, with 25 ns/page in all three rounds.
- `6.17` enters the slower range, with 37 ns/page in all three rounds.
- `6.18`, `6.18.19`, and `6.19.9` remain in the slower range.

So this bare-metal narrowing run reduces the slowdown window from the earlier
`6.16..6.19.9` range to the `v6.16 -> v6.17` release window.  This is not yet a
commit-level root cause; the next step is to inspect commits in `v6.16..v6.17`
that affect the `mprotect()` / PTE permission-change path.

External context: an independent LKML mprotect regression thread bisected a
similar `mprotect()` slowdown to `cac1db8c3aad ("mm: optimize mprotect() by PTE
batching")`.  The bare-metal narrowing result in this directory is consistent
with that direction, but this directory itself records release-window-level
evidence.

Reference:

- https://lkml.iu.edu/2602.1/07208.html
