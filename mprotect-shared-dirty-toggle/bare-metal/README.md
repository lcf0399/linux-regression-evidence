# mprotect shared-dirty toggle bare-metal evidence

Updated: 2026-07-23 UTC

This directory contains physical-machine evidence for a narrow workload:

- a 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping;
- dirty 4 KiB base pages with no THP backing;
- full-range `PROT_READ -> PROT_READ | PROT_WRITE -> write-touch` cycles;
- `iteration_ns_per_page` as the primary lower-is-better metric.

The evidence is scoped to the shared-dirty base-page PTE permission-change
path. It is not a generic `mprotect()` or application-level regression claim.

## Current core evidence

| Directory | Evidence role | Result |
| --- | --- | --- |
| [`20260721-cac1db8c3aad-exact-ab/`](20260721-cac1db8c3aad-exact-ab/) | exact direct-parent/child culprit A/B | `cac1db8c3aad` was `39.77%` slower than the parent midpoint; parent drift was `0.87%` |
| [`20260722-cac1-folio-batch-decomposition-ab/`](20260722-cac1-folio-batch-decomposition-ab/) | same-commit mechanism decomposition | the generic single-PTE commit path and folio lookup explain `43.06%` and `44.47%` of the original gap, or `87.29%` together |
| [`20260722-pedro-v3-exact-ab/`](20260722-pedro-v3-exact-ab/) | matched fix validation | Pedro v3 was `6.20%` slower than the no-v3 midpoint and did not improve this workload |
| [`20260722-v713-shared-pte-hint-fastpath-ab/`](20260722-v713-shared-pte-hint-fastpath-ab/) | candidate path and reverse gate | it was `17.36%` faster for the 4 KiB workload but `65.80%` slower for a PTE-mapped 2 MiB folio, so the candidate was rejected |

The exact culprit A/B and mechanism decomposition ran on an i7-12700KF
physical machine. Every point used a fresh boot, P-core CPU 2, matched
normalized configurations, toolchain and Kbuild metadata, `performance`
governor/EPP, Turbo disabled, and runtime `preempt=none`. Each result
directory records its exact order, sample count, source identity, semantic
checks, and measurements.

## Supporting evidence

[`supporting/`](supporting/) retains three earlier physical-machine results
with independent supporting value:

| Directory | Role | Primary result |
| --- | --- | --- |
| `supporting/20260623-narrow-6.16-6.19-3rounds/` | release-window narrowing | `v6.16=25.000` and `v6.17=37.000 ns/page` |
| `supporting/20260630-single-protect-followup/` | checks that the signal is not specific to repeated toggling | one protect measured `v6.16=8.000` and `v6.17=14.000 ns/page` |
| `supporting/20260706-folio-order-check/` | page-state gate | all nine runs used 4 KiB order-0 pages with no compound/THP backing |

These runs used the earlier i7-14700 platform. They support the release
window, call shape, and page-state interpretation; their absolute timings are
not averaged with the i7-12700KF results.

## Evidence boundary

Early probes, candidate reviews, a conflicted revert, the userfaultfd bridge,
and intermediate ablations superseded by the nine-point decomposition are no
longer current evidence entries.

The preferred citation order is:

1. exact direct-parent/child A/B;
2. nine-point mechanism decomposition;
3. matched fix or candidate validation;
4. release and state-shape evidence under `supporting/`.
