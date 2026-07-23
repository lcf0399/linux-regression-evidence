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
| [nine-point mechanism decomposition](bare-metal/mechanism-comparison.tsv) | parent-style single-PTE update/flush handling and the child `vm_normal_folio()` lookup recover `43.06%` and `44.47%` of the gap, or `87.29%` together | exact-child attribution evidence |
| [matched Pedro v3 on/off](bare-metal/pedro-v3-comparison.tsv) | full v3 was `6.20%` slower than the no-v3 midpoint | did not improve this workload |
| [v7.1.3 shared-PTE hint safety gate](bare-metal/lookup-large-folio-comparison.tsv) | `17.36%` faster for the 4 KiB workload but `65.80%` slower for a PTE-mapped 2 MiB folio | current-code candidate rejected |

## Source-point boundary

The results above intentionally contain two separate source stages:

- The exact A/B and nine-point decomposition use `cac1db8c3aad` and its direct
  parent. In that child, `change_pte_range()` calls `vm_normal_folio()`. The
  `39.77%` exact A/B result and the `43.06%`, `44.47%`, and `87.29%` recovery
  values belong to this source stage.
- The Pedro v3 on/off test and shared-PTE hint gate use the later v7.1.3 code
  stage. Its corresponding lookup calls `vm_normal_page()` and then
  `page_folio()`. The `17.36%` base-page result and `65.80%` large-folio
  reverse result measure the combined current-code bypass and reject the
  simple bypass; they do not isolate `vm_normal_page()` and are not part of
  the exact-commit gap decomposition.

The two result sets are therefore not combined arithmetically.

The current results ran on an i7-12700KF physical machine. Every point used a
fresh boot and P-core CPU 2. Within each comparison, the builds used matched
configurations, toolchain, and Kbuild metadata. Both the scaling governor and
EPP were set to `performance`; Turbo was disabled and runtime
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
