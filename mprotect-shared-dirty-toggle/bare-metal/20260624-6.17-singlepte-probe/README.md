# 2026-06-24 mprotect 6.17 single-PTE probe

This directory records an attribution-only probe. It is not an upstream patch
and not a clean release-kernel A/B result.

Scenario:

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`
- `PIN_CPU=2`
- metric: `iteration_ns_per_page`, lower is better

Probe kernel:

```text
6.17.0-bm-6.17-singlepte-probe
```

The probe changes only the present-PTE path in
`mm/mprotect.c::change_pte_range()` on top of v6.17, restoring the single-PTE
start/commit/flush shape for this 4 KiB shared-dirty base-page workload. The
patch is saved as:

```text
0001-mm-mprotect-probe-6.17-single-pte-hotpath.patch
```

See `source-attribution-note.zh-CN.md` for the source-attribution details and
the exact-revert caveat.

## Result

Three same-boot runs:

```text
kernel                              n  iteration_mean  values  semantic
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25  OK
```

Compared with the previous release-window narrowing:

```text
kernel                              n  iteration_mean  values
6.16.0-bm-6.16                      3          25.000  25 25 25
6.17.0-bm-6.17                      3          37.000  37 37 37
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25
```

All probe runs had `expected_match_ratio=100`, `unexpected_results=0`, 4 KiB
kernel/MMU page size, and no THP.

## Interpretation

This supports the working hypothesis that the slowdown in this standalone
workload comes primarily from the v6.17 PTE-batching shape in
`change_pte_range()`: this workload still behaves like a single-PTE base-page
case, so the added batching/folio helper shape adds cost without useful batch
amortization.

This is not an exact commit revert. Reversing the official
`cac1db8c3aad ("mm: optimize mprotect() by PTE batching")` patch onto the
current `linux-6.17` tree does not apply cleanly, so this directory should be
described as a commit-aligned attribution probe.

This is still not a generic `mprotect()` regression claim. The scope is the
bare-metal shared-dirty 4 KiB full-range protection-toggle workload.
