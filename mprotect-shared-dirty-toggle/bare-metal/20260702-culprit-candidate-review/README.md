# 2026-07-02 culprit-candidate review

This directory records a source-level narrowing step for the upstream-facing
`mprotect()` shared-dirty base-page regression report.

It does **not** claim that a full `git bisect` or exact revert has been
completed.  Its purpose is to make the current culprit hypothesis explicit and
to separate it from the already measured bare-metal timing evidence.

## Current status

The strongest current candidate is:

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

Why this is the current candidate:

- The clean bare-metal release-window run narrows the slowdown to
  `v6.16 -> v6.17`.
- The candidate commit is in that release window.
- It changes `mm/mprotect.c`, specifically the `change_pte_range()` hot path.
- It changes the present-PTE permission update loop from a simple single-PTE
  shape into a batching/folio-helper shape.
- The workload is intentionally 4 KiB shared-dirty base pages with no THP, so
  this path can pay the extra batching/helper cost without getting large-folio
  amortization.
- The v6.17 attribution-only single-PTE hot-path probe brings the measured
  `iteration_ns_per_page` back to the v6.16 range:

```text
6.16.0-bm-6.16                      25 25 25  mean=25.000
6.17.0-bm-6.17                      37 37 37  mean=37.000
6.17.0-bm-6.17-singlepte-probe      25 25 25  mean=25.000
```

The single-protect follow-up also shows that the slowdown is visible in one
prepared-range `mprotect(PROT_READ)`, not only in the repeated toggle loop:

```text
6.16.0-bm-6.16                       8  8  8  mean=8.000
6.17.0-bm-6.17                      14 14 14  mean=14.000
7.1.0-bm-7.1                        18 15 18  mean=17.000
```

## What is still missing

This is not yet an exact culprit proof:

- no full `git bisect` over `v6.16..v6.17`;
- no exact revert of `cac1db8c3aad` tested as a clean release-kernel A/B;
- no claim that this is the only commit in the series that matters.

The earlier attempt to reverse-apply the official patch to the local
`linux-6.17` tree did not apply cleanly:

```text
Hunk #3 FAILED at 177.
Hunk #4 FAILED at 302.
Hunk #5 FAILED at 318.
Hunk #6 FAILED at 350.
4 out of 6 hunks FAILED
```

Therefore the correct wording for upstream is:

```text
The slowdown appears aligned with the v6.17 PTE batching change, especially
cac1db8c3aad ("mm: optimize mprotect() by PTE batching").  I have not completed
an exact revert/bisect yet, but a v6.17 targeted single-PTE hot-path probe brings
this workload back to the v6.16 range.
```

## Relation to existing evidence

- Timing window:
  `../20260623-narrow-6.16-6.19-3rounds/`
- v6.17 attribution probe:
  `../20260624-6.17-singlepte-probe/`
- single-protect follow-up:
  `../20260630-single-protect-followup/`

This directory is a maintainer-facing summary of the current source-level
culprit hypothesis, not a new timing experiment.
