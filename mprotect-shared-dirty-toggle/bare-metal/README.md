# mprotect shared-dirty bare-metal evidence

Updated: 2026-07-23 UTC

This directory contains the compact physical-machine evidence for a narrow
workload:

- a 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping;
- dirty 4 KiB base pages with no THP backing;
- full-range `PROT_READ -> PROT_READ | PROT_WRITE -> write-touch` cycles;
- `iteration_ns_per_page` as the primary lower-is-better metric.

This is not a generic `mprotect()` or application-level regression claim.

## Results

| Evidence | Result |
| --- | --- |
| Exact `cac1db8c3aad` direct-parent/child A/B | child was `39.77%` slower than the parent midpoint; parent drift was `0.87%` |
| Nine-point mechanism decomposition | in the exact child, parent-style single-PTE update/flush recovered `43.06%` of the gap; skipping normal-path batch discovery had no measurable effect; additionally skipping `vm_normal_folio()` recovered `44.47%`; combined recovery was `87.29%` |
| Matched Pedro v3 on/off | full v3 was `6.20%` slower than the no-v3 midpoint and did not improve this workload |
| v7.1.3 shared-PTE hint diagnostic | `17.36%` faster for the 4 KiB workload, but `65.80%` slower for a verified PTE-mapped 2 MiB folio; the current-code candidate was rejected |

The point tables retain every measured per-process value used in these
calculations. The comparison tables retain the midpoint, drift, drop-first,
and recovery calculations.

## Source-point boundary

The exact A/B and nine-point decomposition use the direct
`45199f715b74 -> cac1db8c3aad` source transition. The child calls
`vm_normal_folio()` in `change_pte_range()`. All exact-gap and recovery
percentages belong to that transition.

The Pedro v3 and shared-PTE hint comparisons use the later v7.1.3 code stage,
where the corresponding lookup calls `vm_normal_page()` and then
`page_folio()`. The hint diagnostic also skips batch discovery, so its
`17.36%` result is not a standalone timing of `vm_normal_page()`. At the
earlier exact-child source point, the nested sequence separately isolated a
measurable `vm_normal_folio()` cost and found no measurable batch-discovery
cost. The v7.1.3 result is current-code corroboration, not an isolated lookup
measurement, and its positive and reverse-gate percentages are not folded
into the exact-commit gap decomposition.

## Measurement contract

The current attribution results ran on an Intel Core i7-12700KF system with
32 GiB RAM. Each point used a fresh boot, P-core CPU 2, matched normalized
configurations, GCC 15.2.0 and Kbuild metadata, `performance` governor/EPP,
Turbo disabled, and runtime `preempt=none`.

The base-page points used three external warm-up processes and 15 measured
processes. Each measured process ran 1,000 cycles after 10 internal warm-ups.
The large-folio reverse gate used two external warm-ups, 15 measured
processes, 200 cycles, and five internal warm-ups. All measured processes
passed the return-value and page-shape gates; `run-audit.tsv` records the
distinct boot IDs and zero failed systemd units.

## Compact evidence files

| Files | Role |
| --- | --- |
| `exact-cac1-{points,components,comparison}.tsv` | exact direct-parent/child result |
| `mechanism-{points,components,comparison}.tsv` | nine-point exact-child attribution sequence |
| `pedro-v3-{points,components,comparison}.tsv` | matched no-v3/full-v3 result |
| `lookup-base-page-{points,comparison}.tsv` | v7.1.3 4 KiB page/folio-lookup positive gate |
| `lookup-large-folio-{points,comparison,shape}.tsv` | v7.1.3 PTE-mapped large-folio reverse gate |
| `lookup-trace.tsv` | paired function-entry counts |
| `source-identity.tsv` | source, code-state, patch, config, compiler, and release identities |
| `run-audit.tsv` | boot identity, sample count, semantic failures, and failed units |
| `patches/` | the four exact diagnostic diffs used by the attribution tests |
| `supporting/` | compact release-window, single-protect, and base-page state checks |

Build logs, objdump output, installation logs, warm-up output, repeated
per-point environment snapshots, and duplicate workload copies are
rebuildable intermediates and are intentionally excluded. The standalone
base-page and large-folio reproducers are in `../reproducer/`.

## Diagnostic patches

The patches are attribution probes, not proposed fixes:

| Patch | Diagnostic role |
| --- | --- |
| `0000-diagnostic-single-pte-parent-style-commit-fastpath.patch` | keep child lookup/discovery, restore parent-style single-PTE update/flush |
| `0001-diagnostic-keep-folio-skip-batch-direct-single-pte.patch` | retain folio lookup while bypassing normal batch discovery |
| `0002-diagnostic-skip-folio-and-batch-direct-single-pte.patch` | bypass both normal folio lookup and batch discovery |
| `0001-RFC-mm-mprotect-avoid-shared-folio-lookup-without-batch-hint.patch` | v7.1.3 `vm_normal_page()` plus `page_folio()` diagnostic used for the positive and reverse gates |
