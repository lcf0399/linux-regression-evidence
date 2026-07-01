# mprotect shared-dirty toggle bare-metal results

Updated: 2026-06-30 UTC

This directory contains the i7-14700 bare-metal reruns for the standalone
`mprotect()` shared-dirty toggle reproducer.  It checks whether the earlier
QEMU/lab signal also appears on physical hardware, and whether the Pedro v3
patch-only kernel improves this standalone workload.

## Platform

```text
CPU: Intel Core i7-14700, 28 logical CPUs, 1 NUMA node
pinning: taskset -c 2
metric: iteration_ns_per_page, lower is better
scenario: shared_dirty_full_toggle_64m
mapping: 64 MiB shared dirty, 4 KiB pages, no THP
rounds: 9 external rounds
```

`iteration_ns_per_page` is the wall-clock time for one full
protect/restore/write-touch iteration divided by the number of 4 KiB pages in
the range.

## Bare-metal A/B

Result directory:

```text
20260622-20260623-ab/
```

| Kernel | Result dir | iteration_ns_per_page | protect | restore | post-touch | State |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `6.12.77-bm-6.12.77` | `20260622T170142Z_6.12.77-bm-6.12.77` | 27 | 9 | 8 | 9 | 4 KiB/no THP, semantic OK |
| `6.19.9-bm-6.19.9` | `20260622T165808Z_6.19.9-bm-6.19.9` | 37 | 14 | 14 | 8 | 4 KiB/no THP, semantic OK |
| `6.19.9-bm-6.19.9-pedro-v3` | `20260623T054659Z_6.19.9-bm-6.19.9-pedro-v3` | 39 | 15 | 15 | 8 | 4 KiB/no THP, semantic OK |
| `7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57` | `20260622T180559Z_7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57` | 39 | 15 | 15 | 8 | 4 KiB/no THP, semantic OK |

Interpretation:

- On this bare-metal node, `6.19.9` is slower than `6.12.77`, matching the
  direction of the earlier QEMU/lab candidate signal.
- `6.19.9 + Pedro v3 patch-only` does not bring this standalone workload back
  to the `6.12.77` range, and does not improve over the `6.19.9` original.
- `mm-unstable-pedro-444fc9435e57` is not a strict patch-only comparison; it is
  retained as later baseline context only.

A single-point `6.16` smoke run was used only to confirm that the standalone
reproducer and state check worked on this machine.  It is not an evidence
entry and the raw smoke result is not retained here.

## 2026-06-23 5-kernel queue context

Result directory:

```text
20260623-kernel-queue-5kernels-3rounds/
```

This early queue ran `6.12.77`, `6.19.9`, `6.19.9 + Pedro v3`, `7.0.9`, and
`mm-unstable-pedro` three times each.  It is useful context for the Pedro v3
patch-only check, but it is not the release-window narrowing result.

## 2026-06-23 base-page attribution probe

Result directory:

```text
20260623-basepage-probe/
```

This is not a clean release-kernel A/B.  It is a temporary probe patch on
`6.19.9` that adds a single-PTE fast path in `change_pte_range()` for the
resident base-page path, to test whether this workload's cost is coming from
the base-page path going through folio/batching helpers.

Three same-boot runs:

```text
kernel                                n  iteration_mean  protect_mean  restore_mean  post_touch_mean
6.19.9-bm-6.19.9-basepage-probe       3          30.333        10.667        10.000            9.000
```

Compared with clean kernels from `20260623-kernel-queue-5kernels-3rounds/`:

```text
6.12.77-bm-6.12.77                    3          26.000         9.000         8.000            8.000
6.19.9-bm-6.19.9                      3          37.000        14.000        14.000            8.000
```

The probe recovers part of the `6.19.9` cost, but does not fully return to the
`6.12.77` range.  This kept the working hypothesis focused on the base-page
PTE loop entering the folio/batching helper shape, while motivating a narrower
release-window check.

## 2026-06-23 release-window narrowing

Result directory:

```text
20260623-narrow-6.16-6.19-3rounds/
```

This run interleaved five clean kernels across three boot/run rounds:

```text
6.16.0-bm-6.16
6.17.0-bm-6.17
6.18.0-bm-6.18
6.18.19-bm-6.18.19
6.19.9-bm-6.19.9
```

Main metric: `iteration_ns_per_page`, lower is better.

```text
kernel                 n  iteration_mean  iteration_cv_pct  values
6.16.0-bm-6.16        3          25.000             0.000  25 25 25
6.17.0-bm-6.17        3          37.000             0.000  37 37 37
6.18.0-bm-6.18        3          38.000             0.000  38 38 38
6.18.19-bm-6.18.19    3          38.000             0.000  38 38 38
6.19.9-bm-6.19.9      3          36.667             1.286  37 36 37
```

All steps reported `expected_match_ratio=100` and `unexpected_results=0`.

This narrows the slowdown to the `v6.16 -> v6.17` release window.  It is not a
commit-level root cause by itself.

## 2026-06-24 v6.17 single-PTE attribution probe

Result directory:

```text
20260624-6.17-singlepte-probe/
```

This is an attribution-only probe, not a clean release-kernel A/B.  On top of
`v6.17`, it changes only the present-PTE hot path in
`mm/mprotect.c::change_pte_range()` back to a single-PTE start/commit/flush
shape for the path exercised by this 4 KiB shared-dirty base-page workload.

Three same-boot runs:

```text
kernel                              n  iteration_mean  values
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25
```

Aligned with the release-window data:

```text
6.16.0-bm-6.16                      3          25.000  25 25 25
6.17.0-bm-6.17                      3          37.000  37 37 37
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25
```

All probe runs passed the semantic/state checks, and the state shape remained
4 KiB/no THP.

The probe brings `v6.17` back to the `v6.16` fast range for this workload,
which strongly points at the `v6.17` PTE-batching hot-path shape as the main
cost.  It is still mechanism-attribution evidence, not an upstream patch and
not a generic `mprotect()` regression claim.

The source-attribution and exact-revert caveat are recorded in:

```text
20260624-6.17-singlepte-probe/source-attribution-note.zh-CN.md
```

## 2026-06-30 single-protect follow-up

Result directory:

```text
20260630-single-protect-followup/
```

This follow-up checks whether the slowdown only appears in the repeated
protect/restore loop.  Each timed iteration creates a fresh shared-dirty
mapping, write-prefaults it, and times exactly one `mprotect(PROT_READ)`.

Main metric: `single_protect_ns_per_page`, lower is better.

```text
kernel                 n  single_protect_mean  values
6.16.0-bm-6.16        3                 8.000  8 8 8
6.17.0-bm-6.17        3                14.000  14 14 14
7.1.0-bm-7.1          3                17.000  18 15 18
```

All steps reported `expected_match_ratio=100` and `unexpected_results=0`.

This shows that the `v6.16 -> v6.17` slowdown is visible even for a single
`mprotect(PROT_READ)` on the prepared shared-dirty range.  It is supporting
evidence for the existing mprotect report, not a separate claim.
