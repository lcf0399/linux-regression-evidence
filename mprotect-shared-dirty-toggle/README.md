# mprotect shared-dirty toggle evidence

This directory supports the narrowly scoped upstream report:

```text
[REGRESSION] mm/mprotect: shared-dirty base-page toggle slower since v6.17
```

## Scope

The workload uses a 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping, dirties its
4 KiB base pages before timing, and repeatedly performs:

```text
mprotect(PROT_READ)
mprotect(PROT_READ | PROT_WRITE)
write-touch
```

The primary lower-is-better metric is `iteration_ns_per_page`. This is not a
generic `mprotect()` or application-level regression claim.

## Current conclusions

| Evidence | Result | Status |
| --- | --- | --- |
| [exact `cac1db8c3aad` parent/child A/B](bare-metal/exact-cac1-comparison.tsv) | the child was `39.77%` slower than the parent midpoint; parent drift was `0.87%` | current commit-level culprit |
| [nine-point mechanism decomposition](bare-metal/mechanism-comparison.tsv) | the generic single-PTE commit path and folio lookup explain `43.06%` and `44.47%` of the gap, or `87.29%` together | attribution evidence |
| [matched Pedro v3 on/off](bare-metal/pedro-v3-comparison.tsv) | full v3 was `6.20%` slower than the no-v3 midpoint | did not improve this workload |
| [shared-PTE hint reverse gate](bare-metal/lookup-large-folio-comparison.tsv) | `17.36%` faster for the 4 KiB workload but `65.80%` slower for a PTE-mapped 2 MiB folio | candidate rejected |

The exact attribution ran on an i7-12700KF physical machine. Every point used
a fresh boot, P-core CPU 2, matched configurations, toolchain and Kbuild
metadata, `performance` governor/EPP, Turbo disabled, and runtime
`preempt=none`. Every measured process used for the conclusions passed the
return-value and page-state checks.

## Supporting evidence

[`bare-metal/supporting/`](bare-metal/supporting/) retains three physical-
machine results with independent supporting value:

- release window: `v6.16=25.000` and `v6.17=37.000 ns/page`;
- single protect: `v6.16=8.000` and `v6.17=14.000 ns/page`;
- folio-order gate: all nine runs used 4 KiB order-0 pages with no
  compound/THP backing.

These results establish the release window, call shape, and state shape. They
do not replace the exact commit-level A/B.

## Earlier non-bare-metal context

In early QEMU/lab screening, `v6.19.9` was about `1.63–1.67x` slower than
`v6.12.77` at the 1/2/4-vCPU points. An mm-unstable/Pedro v3 result initially
looked like a partial improvement, but it was not a matched patch-only A/B.
The later same-source bare-metal on/off test measured `+6.20%`, so the early
interpretation is retired.

Detailed non-bare-metal material is retained only in the local experiment
archive and is intentionally excluded from this public evidence bundle.

## Directories

- [`bare-metal/`](bare-metal/): current physical-machine conclusions and
  supporting evidence.
- [`reproducer/`](reproducer/): standalone base-page and large-folio
  reproducers.

Superseded physical-machine diagnostics with unique historical information
are retained locally and are intentionally excluded from this public evidence
bundle.
