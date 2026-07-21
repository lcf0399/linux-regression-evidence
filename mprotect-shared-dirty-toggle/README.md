# mprotect Shared-dirty Toggle Evidence

This directory currently supports the upstream-facing report:

```text
[REGRESSION] mm/mprotect: shared-dirty base-page toggle slower since v6.17
```

## Claim scope

This is a deliberately narrow userspace-visible `mprotect()` workload:

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping
- prefaulted and write-dirtied 4 KiB base pages, no THP
- repeated full-range `mprotect(PROT_READ)`
- restore with `mprotect(PROT_READ | PROT_WRITE)`
- write-touch after each protect/restore cycle

It does not claim a generic `mprotect()` regression, and does not claim `anon_full_toggle` or THP mprotect regression.

## Current Bare-metal Result

The strongest current evidence is an exact direct-parent/child sandwich around
`cac1db8c3aad ("mm: optimize mprotect() by PTE batching")` on an i7-12700KF
bare-metal system:

| Point | Commit | n | mean `iteration_ns_per_page` |
| --- | --- | ---: | ---: |
| parent A | `45199f715b74` | 15 | 38.133 |
| child | `cac1db8c3aad` | 15 | 53.533 |
| parent B | `45199f715b74` | 15 | 38.467 |

The child is `39.77%` slower than the midpoint of the two parent controls,
while the parent drift is only `0.87%`. Dropping the first measured process
from every point leaves a `39.53%` delta. All 45 measured processes passed the
semantic and 4 KiB/no-THP state checks.

This is exact commit-level culprit evidence for this narrow workload, not a
generic `mprotect()` or application-level regression claim. The complete
source/build identity and raw measurements are in:

```text
bare-metal/20260721-cac1db8c3aad-exact-ab/
```

The earlier i7-14700 standalone rerun narrowed the slowdown to the
`v6.16 -> v6.17` release window and supplied the following release context.

Main metric: `iteration_ns_per_page`, lower is better.

| Kernel | values | mean |
| --- | --- | ---: |
| `v6.16` | 25 25 25 | 25.000 |
| `v6.17` | 37 37 37 | 37.000 |
| `v6.18` | 38 38 38 | 38.000 |
| `v6.18.19` | 38 38 38 | 38.000 |
| `v6.19.9` | 37 36 37 | 36.667 |

All runs reported `expected_match_ratio=100` and `unexpected_results=0`.

An attribution-only v6.17 single-PTE probe brings the standalone result back to
the v6.16 fast range:

| Kernel | values | mean |
| --- | --- | ---: |
| `v6.16` | 25 25 25 | 25.000 |
| `v6.17` | 37 37 37 | 37.000 |
| `v6.17 single-PTE probe` | 25 25 25 | 25.000 |

That probe is not an exact commit revert and is not proposed as an upstream
patch.  It is mechanism-attribution evidence pointing at the v6.17
PTE-batching hot-path shape in `mm/mprotect.c::change_pte_range()` for this
workload.

A dedicated culprit-candidate review records the current source-level
hypothesis:

```text
bare-metal/20260702-culprit-candidate-review/
```

That review identified `cac1db8c3aad ("mm: optimize mprotect() by PTE
batching")` as the strongest candidate. The later exact direct-parent/child
test above now confirms it as the source of the measured slowdown in this
workload; a full bisect or a conflicted v6.17 revert is no longer needed for
commit attribution.

A follow-up revert attempt is recorded in:

```text
bare-metal/20260702-cac1db8c3aad-revert-attempt/
```

Direct `git revert --no-commit cac1db8c3aad` conflicts on the real `v6.17` tag
because later `mm/mprotect.c` edits are layered on top.  A synthesized
`v6.17` mprotect-only minus-cac candidate passed build/install and was timed on
bare metal:

| Kernel | values | mean |
| --- | --- | ---: |
| `v6.16` | 25 25 25 | 25.000 |
| `v6.17` | 38 36 36 | 36.667 |
| `v6.17 mprotect-only minus-cac1db8c3aad` | 27 27 26 | 26.667 |

This is not a clean exact-revert proof, because direct revert conflicts on
`v6.17`; however it is stronger mechanism evidence than the earlier
single-PTE probe and points at the batching change as the relevant mechanism
for this workload.

`v6.19.9 + Pedro v3 patch-only` and the later mm-unstable/Pedro follow-up did
not improve this standalone bare-metal result.

## 2026-06-30 single-protect follow-up

To check whether the signal only comes from repeated `RW -> R -> RW` toggling,
I added a narrower follow-up where each timed iteration creates a fresh
shared-dirty mapping and times exactly one `mprotect(PROT_READ)`.

Result directory:

```text
bare-metal/20260630-single-protect-followup/
```

Main metric: `single_protect_ns_per_page`, lower is better.

| Kernel | values | mean | vs v6.16 |
| --- | --- | ---: | ---: |
| `v6.16` | 8 8 8 | 8.000 | baseline |
| `v6.17` | 14 14 14 | 14.000 | +75.0% |
| `v7.1` | 18 15 18 | 17.000 | +112.5% |

All runs reported `expected_match_ratio=100` and `unexpected_results=0`.

This shows that a single `mprotect(PROT_READ)` on the prepared shared-dirty
range already reproduces the `v6.16 -> v6.17` slowdown.  It is supporting
evidence for the same `mprotect()` PTE update path, not a separate regression
claim.

A later folio-order state-shape check is recorded in:

```text
bare-metal/20260706-folio-order-check/
```

It reads pagemap/kpageflags for the same 64 MiB shared-dirty base-page shape.
Across `6.16.0-bm-6.16`, `6.17.0-bm-6.17`, and `7.1.0-bm-7.1`, all nine runs
reported 16384 present pages, 4 KiB `KernelPageSize`/`MMUPageSize`, and zero
`KPF_COMPOUND_HEAD`, `KPF_COMPOUND_TAIL`, and `KPF_THP` pages.  This is
state-shape attribution evidence that the tested workload was not a
PTE-mapped compound/THP folio case.

A related userfaultfd bulk write-protect bridge is recorded in:

```text
bare-metal/20260709-userfaultfd-bulk-wp-bridge/
```

This is not a separate `mm/userfaultfd.c` regression claim.  It checks another
entry point into the same PTE permission-change machinery.  In five interleaved
bare-metal batches, `bulk_writeprotect_ioctl_1024m` measured `6.16=25.720`,
`6.17=33.544`, and `6.17-minus-cac1db8c3aad=26.040 ns/page`.  The
`minus-cac` kernel is a hand-adapted mprotect-only mechanism candidate, not a
clean exact revert, but it pulls the userfaultfd bulk-WP result back toward the
`6.16` range.

## Earlier non-bare-metal context

Earlier QEMU/lab runs were used only to screen this workload and guide the
later physical-machine experiments. Their detailed results, profiles, state
audits, and experiment-framework files remain local and are intentionally not
part of this public evidence bundle. The claims above rely on the evidence in
`bare-metal/`.

## Directories

- `bare-metal/`: physical-machine results and source-attribution evidence.
- `reproducer/`: standalone C reproducer and helper script for maintainer-side
  quick checks outside the experiment framework.
- `workload/`: userspace workload source kept for semantic auditability.
