# 2026-06-23 mprotect base-page probe

This directory contains a bare-metal attribution probe. It does not use QEMU.
The kernel is `6.19.9` plus a temporary resident base-page single-PTE fast path
inside `change_pte_range()`, before the path enters `mprotect_folio_pte_batch()`.

This is not an upstream-ready patch and is not a clean release-kernel A/B. The
probe patch is archived here:

- `0001-mm-mprotect-probe-basepage-single-pte-fastpath.patch`

Scenario:

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`

Metric direction:

- `iteration_ns_per_page`: lower is better
- `protect_ns_per_page`, `restore_ns_per_page`, and `post_touch_ns_per_page`
  are phase-level metrics
- `expected_match_ratio=100` and `unexpected_results=0` mean the semantic
  check passed

Result:

```text
kernel                                n  iteration_mean  protect_mean  restore_mean  post_touch_mean
6.19.9-bm-6.19.9-basepage-probe       3          30.333        10.667        10.000            9.000
```

Clean-kernel context from the same bare-metal queue:

```text
kernel                                n  iteration_mean  protect_mean  restore_mean  post_touch_mean
6.12.77-bm-6.12.77                    3          26.000         9.000         8.000            8.000
6.19.9-bm-6.19.9                      3          37.000        14.000        14.000            8.000
6.19.9-bm-6.19.9-pedro-v3             3          39.000        15.000        15.000            8.333
```

Current interpretation: the probe lowers `6.19.9` from about 37 to about 30
`iteration_ns_per_page`, so the base-page resident PTE path's folio/batching
helper shape appears to contribute part of the cost. It does not fully return
to the `6.12.77` level of about 26, so the remaining difference still needs
release-window or more focused source attribution.
