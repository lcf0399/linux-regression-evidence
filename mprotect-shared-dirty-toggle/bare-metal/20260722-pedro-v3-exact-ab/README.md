# Exact Pedro v3 on/off A/B in the current environment

## Result

Short answer: **Pedro v3 did not improve this narrow workload.** On the current
i7-12700KF bare-metal system, with matched Linux v7.1.3 sources and build
conditions, full v3 was **6.20% slower** than the midpoint of the surrounding
no-v3 controls. Lower is better.

| Point | v3 | n | Mean ns/page | SD | CV | Range |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| no-v3 A | off | 15 | 55.333 | 0.617 | 1.12% | 54-56 |
| Pedro v3 | on | 15 | 58.200 | 0.561 | 0.96% | 57-59 |
| no-v3 B | off | 15 | 54.267 | 0.594 | 1.09% | 53-55 |

The no-v3 midpoint was `54.800 ns/page`. Control drift was `-1.93%`. Dropping
the first measured process from every point left the v3 delta at `+6.18%` and
control drift at `-1.93%`. The 15 v3 values (`57-59`) did not overlap the 30
control values (`53-56`).

The component means place the difference in the `mprotect()` phases, not the
later write-touch:

| Point | Protect ns/page | Restore ns/page | Post-touch ns/page |
| --- | ---: | ---: | ---: |
| no-v3 A | 21 | 21 | 11.533 |
| Pedro v3 | 23 | 23 | 11.533 |
| no-v3 B | 21 | 21 | 11.733 |

Thus, for this specific 64 MiB shared-dirty, 4 KiB base-page, full-range toggle
microbenchmark, the exact test found a small stable slowdown rather than an
improvement. This result does not claim that v3 slows other CPUs, toolchains,
folio shapes, `mprotect()` patterns, or applications.

## Current patch and source identity

As of 2026-07-22, v3 remains the latest public revision found; no v4/v5 was
found. It entered mainline through `40735a683bf8`, is present from `v7.1-rc1`,
and is in `v7.1.3`. A Torvalds-tree mirror refreshed to
`origin/master=248951ddc14d` showed no later `mm/mprotect.c` implementation
change. See `upstream-status.tsv`.

Both kernels came from the same official `v7.1.3` source archive:

- no-v3 replaced only `mm/mprotect.c` with the exact file at the series base
  `19999e479c2a38672789e66b4830f43c645ca1f2`;
- full-v3 used unmodified `v7.1.3`; its `mm/mprotect.c` exactly matches series
  tip `89e613bc0b2d6d4a18a09b161131ce4ca5c70f2a`.

`3bc181c14363` is a direct child of `19999e479c2a`, `89e613bc0b2d` is a direct
child of `3bc181c14363`, and both patches modify only `mm/mprotect.c`. A
pre-config source-tree gate also reported only that file. This is therefore a
matched v7.1.3 no-v3/full-v3 reconstruction, not a newly claimed upstream
direct-parent commit pair.

The builds used the same normalized config, GCC 15.2.0 toolchain, Kbuild
metadata, external module-signing identity, dynamic-preempt build, runtime
`preempt=none`, and equal-length release strings.

## Workload and run contract

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping;
- prefault and dirty every 4 KiB page;
- repeatedly apply `PROT_READ`, restore `PROT_READ|PROT_WRITE`, then write-touch;
- 1,000 iterations plus 10 internal warm-ups per measured process;
- three external warm-up processes and 15 measured processes per point;
- `no-v3 A -> full-v3 -> no-v3 B`, with a fresh boot for every point;
- P-core CPU 2, performance governor/EPP, Turbo disabled.

All 45 measured processes passed the returned-value and page-state checks:
`expected_match_ratio=100`, no unexpected results, 4 KiB kernel/MMU pages, no
THP, and no failed systemd units.

After the first no-v3 boot, the remote orchestrator falsely rejected
`preempt=none` because of a shell-quoting bug. The actual command line contained
the token and no measurement had begun. After correcting the guard, the same
fresh boot received the complete 60-second settle, kernel identity check, CPU profile,
and timing sequence. Its uptime was consequently longer; control drift and
drop-first sensitivity are reported above.
