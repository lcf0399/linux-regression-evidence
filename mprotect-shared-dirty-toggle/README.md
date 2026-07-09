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

The current maintainer-facing evidence is the i7-14700 bare-metal standalone
rerun.  It narrows the slowdown to the `v6.16 -> v6.17` release window.

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

The strongest current candidate is `cac1db8c3aad ("mm: optimize mprotect() by
PTE batching")`.  This is a strong candidate/series direction, not an exact
culprit proof: no full `git bisect` or clean exact-revert A/B has been completed
yet.

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

Related exploratory `mmap_lock` and `mmu_notifier` routes also observed timing
signals, but split/no-KVM/KVM attribution showed their main difference comes
back to the `mprotect()` permission-change/restore path.  They are kept as
supporting attribution only, not as independent upstream claims.

## Earlier Lab/QEMU Context

The earlier formal lab timing, before the bare-metal rerun, showed `v6.19.9`
slower than `v6.12.77`.  This is kept as historical candidate evidence and
background context, not as the current upstream-facing conclusion.

`cycle_ns_per_page`:

| CPU | v6.12.77 | v6.19.9 | delta | reliability |
| --- | ---: | ---: | ---: | --- |
| 1 | 346.8 | 578.1 | -40.0% | clean reliable |
| 2 | 394.7 | 641.7 | -38.5% | robust-only |
| 4 | 381.1 | 624.8 | -39.0% | partial same direction |

Separate release-level sanity checks showed `v6.18.19` already in the slow
range, but those raw runs are kept out of this compact public evidence bundle.

## Earlier mm-unstable Lab Follow-up

David Hildenbrand pointed to Pedro Falcato's recent small-folio mprotect
optimization. A lab sanity matrix against `akpm/mm mm-unstable
444fc9435e57` shows partial mitigation in this workload, but not a return
to `v6.12.77` timing:

This section is earlier QEMU/lab follow-up context.  It should not be merged
with the later bare-metal standalone result above.

| CPU | v6.12.77 | v6.19.9 | mm-unstable | mm-unstable vs v6.19 | gap closed |
|---:|---:|---:|---:|---:|---:|
| 1 | 336.1 | 532.0 | 497.0 | 6.6% faster | 17.9% |
| 2 | 369.2 | 581.9 | 503.3 | 13.5% faster | 36.9% |
| 4 | 355.7 | 587.2 | 524.2 | 10.7% faster | 27.2% |
| 8 | 369.7 | 583.6 | 534.2 | 8.5% faster | 23.1% |
| 16 | 374.8 | 607.1 | 547.8 | 9.8% faster | 25.5% |

The 16 CPU row has one `v6.12.77` QEMU failure in that sanity matrix, so it
is supporting trend evidence only.

A separate state-shape audit checked whether this mprotect comparison has a
`MADV_PAGEOUT`-style caveat where kernels operate on materially different
page/VMA state. The state audit found the successful `v6.12.77`, `v6.19.9`,
and `mm-unstable` runs all using the same 4 KiB shared-dirty PTE mapping
shape: 16384 present pages before/after, no THP backing, one final VMA, and
no semantic mismatches. That supports treating the remaining timing gap as a
same-state implementation-path cost rather than a mismatched workload-state
comparison.

## Directories

- `workload/`: generated workload source used by the framework.
- `reproducer/`: standalone C reproducer and helper script for maintainer-side
  quick checks outside the experiment framework.
- `reproducer-validation/`: lab validation summary for the standalone
  reproducer.
- `experiments/`: formal experiment profile.
- `formal-lab-summary/`: compact public summary of the original formal lab
  timing and direct-hit coverage evidence. The original raw runner output has
  been moved to the ignored local-only `local-archive/` directory.
- `mm-unstable-followup-summary/`: compact follow-up summary for the small-folio
  optimization discussion. The original lab/local raw data has been moved to
  the ignored local-only `local-archive/` directory.
- `state-audit-summary/`: compact public summary of the lab state-shape audit
  supporting the same-state comparison assumption. The original raw lab output
  has been moved to the ignored local-only `local-archive/`.
- `bare-metal/`: i7-14700 bare-metal rerun results. The standalone A/B still
  shows `6.19.9` slower than `6.12.77`; `6.19.9 + Pedro v3` patch-only did
  not improve this standalone result. A later release-window narrowing shows
  the slowdown appears in the `v6.16 -> v6.17` window, and a v6.17
  attribution-only single-PTE probe brings the standalone metric back to the
  v6.16 fast range. That supports the working hypothesis that the cost in this
  workload comes from the v6.17 PTE-batching shape in
  `mm/mprotect.c::change_pte_range()`. The probe is not an exact commit
  revert; see
  `bare-metal/20260624-6.17-singlepte-probe/source-attribution-note.zh-CN.md`.
  `bare-metal/20260702-culprit-candidate-review/` records the current strongest
  candidate, `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`, while
  keeping the no-full-bisect/no-exact-revert caveat explicit.
  `bare-metal/20260702-cac1db8c3aad-revert-attempt/` records the first
  follow-up exact-revert attempt: direct git revert conflicts on `v6.17`, while
  a synthesized mprotect-only minus-cac candidate builds, installs, and brings
  the bare-metal metric back close to the `v6.16` fast range.
  The later `bare-metal/20260630-single-protect-followup/` directory shows that
  a single protect operation also reproduces the same release-window slowdown.
  `bare-metal/20260706-folio-order-check/` records the no-compound/no-THP
  state-shape check, and `bare-metal/20260709-userfaultfd-bulk-wp-bridge/`
  records a related userfaultfd bulk-WP cross-entry check that is pulled back by
  the same mprotect-only minus-cac mechanism candidate.

For the formal lab and follow-up matrices, the public bundle keeps compact
metric summaries. Full runner directories, raw CSV/JSON, pipeline metadata, and
verbose launch logs remain under the local-only `local-archive/` directory
unless they become necessary for debugging.
