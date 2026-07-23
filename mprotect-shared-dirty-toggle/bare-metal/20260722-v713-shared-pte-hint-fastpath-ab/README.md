# v7.1.3 shared single-PTE hint fast path: positive and reverse gates

Updated: 2026-07-22 UTC

## Result

This experiment turned the measured page/folio lookup cost into a runnable
diagnostic candidate, but the reverse gate rejected it as a possible fix:

- in the original 64 MiB shared-dirty 4 KiB base-page workload, the candidate
  was `17.36%` faster than the midpoint of the surrounding unmodified v7.1.3
  controls;
- a paired trace removed exactly `2,048` calls to both `vm_normal_page()` and
  `mprotect_folio_pte_batch()`, matching the two 1,024-PTE protect and restore
  walks;
- in a verified 2 MiB PTE-mapped large-folio workload, the same candidate was
  `65.80%` slower.

The normal-path page/folio lookup therefore has measurable cost in the
base-page workload, but `pte_batch_hint() == 1` is not a safe x86 base-page
test. The patch is retained only as an attribution probe and is not proposed
as an upstream fix.

## Source and build identity

Both kernels came from the same official v7.1.3 source snapshot
`199c9959d3a9b53f346c221757fc7ac507fbac50`. Its unmodified `mm/mprotect.c`
matches the Pedro v3 series tip
`89e613bc0b2d6d4a18a09b161131ce4ca5c70f2a`.

| Role | Kernel release | Change |
| --- | --- | --- |
| baseline | `7.1.3-mprotect-pv3-full-89e613bc0b2d` | unmodified v7.1.3 |
| candidate | `7.1.3-mprotect-hint-one-000000000000` | only the diagnostic patch in this directory |

For a non-NUMA `VM_SHARED` PTE, the candidate skips `vm_normal_page()` and
sets `nr_ptes=1` when `pte_batch_hint() == 1`. The two builds used the same
canonical configuration, GCC 15.2.0 toolchain, Kbuild metadata, signing key,
and runtime `preempt=none`.

## 4 KiB base-page positive gate

The order was `baseline A -> candidate -> baseline B`, with a fresh boot for
each point. Each point used three warm-up processes and 15 measured processes.

| Point | n | Mean iteration ns/page | CV | Semantic failures |
| --- | ---: | ---: | ---: | ---: |
| baseline A | 15 | 56.933 | 0.804% | 0 |
| candidate | 15 | 47.133 | 1.096% | 0 |
| baseline B | 15 | 57.133 | 0.904% | 0 |

The candidate was `17.358%` faster than the baseline midpoint, while baseline
drift was `0.351%`. Dropping the first measured process from every point left
the delta at `-17.323%`.

The paired non-timing trace was:

| Role | `change_pte_range` | `vm_normal_page` | `mprotect_folio_pte_batch` |
| --- | ---: | ---: | ---: |
| baseline | 12 | 10,996 | 2,064 |
| candidate | 12 | 8,948 | 16 |

## PTE-mapped large-folio reverse gate

The reproducer created a shared 2 MiB shmem folio, split its PMD mapping into
512 PTE mappings, and faulted the PTEs in before timing. Every measured process
confirmed 4 KiB kernel/MMU page sizes, 512 present PTEs, one compound head,
511 compound tails, and `KPF_THP` on all 512 pages.

The lower-is-better metric below includes only the protect and restore phases:

| Point | n | Protect | Restore | Total ns/page | CV | Semantic failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline A | 15 | 12.000 | 11.000 | 23.000 | 0.000% | 0 |
| candidate | 15 | 19.133 | 19.000 | 38.133 | 0.923% | 0 |
| baseline B | 15 | 12.000 | 11.000 | 23.000 | 0.000% | 0 |

The candidate was `65.797%` slower than the baseline midpoint; baseline drift
was zero. On x86, `pte_batch_hint()` also returns one for this PTE-mapped
large-folio shape, so the candidate disables existing folio batching.

## Evidence files

- `summary.tsv`, `sensitivity.tsv`, and `decision.tsv`: base-page result;
- `runtime-trace-summary.tsv`: paired function-entry counts;
- `large-folio-summary.tsv`, `large-folio-sensitivity.tsv`, and
  `large-folio-decision.tsv`: reverse-gate result;
- `runs/` and `large-folio-runs/`: measured processes and environment records;
- `manifests/`, `build-logs/`, and `install-logs/`: source and build identity;
- `workload/` and `mprotect_shared_pte_mapped_thp_reproducer.c`: reproducers;
- `terminal-state.tsv`: restored final machine state.
