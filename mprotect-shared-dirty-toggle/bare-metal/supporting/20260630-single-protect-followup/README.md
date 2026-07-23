# Single-protect bare-metal follow-up

This is a follow-up for the upstream-facing
`mprotect-shared-dirty-toggle` report.  It checks whether the slowdown only
appears in the repeated `RW -> R -> RW` toggle loop, or whether a single
`mprotect(PROT_READ)` on a prepared shared-dirty mapping is already slower.

It is not a separate regression claim.  It is supporting evidence for the same
`mm/mprotect.c` permission-change path.

## Workload

Each timed iteration does:

1. `mmap(MAP_SHARED | MAP_ANONYMOUS, PROT_READ | PROT_WRITE)`;
2. `MADV_NOHUGEPAGE`;
3. write-prefault the full 64 MiB range;
4. check the state shape with `/proc/self/smaps`;
5. time exactly one `mprotect(PROT_READ)`;
6. `munmap()`.

The primary metric is `single_protect_ns_per_page`, lower is better.  Setup and
total time are recorded as secondary checks.

## Run parameters

```text
CPU: Intel Core i7-14700, 28 logical CPUs, 1 NUMA node
pinning: taskset -c 2
mapping: 64 MiB shared dirty, 4 KiB pages, no THP
iterations: 200 timed iterations per external round
warmup: 5 iterations
external rounds: 5 per boot/run step
queue: v6.16 -> v6.17 -> v7.1, repeated 3 times
```

## Result

Summary:

| Kernel | n | `single_protect_ns_per_page` values | mean | vs v6.16 | state |
| --- | ---: | --- | ---: | ---: | --- |
| `v6.16` | 3 | 8 8 8 | 8.000 | baseline | 4 KiB/no THP, semantic OK |
| `v6.17` | 3 | 14 14 14 | 14.000 | +75.0% | 4 KiB/no THP, semantic OK |
| `v7.1` | 3 | 18 15 18 | 17.000 | +112.5% | 4 KiB/no THP, semantic OK |

All steps reported `expected_match_ratio=100` and `unexpected_results=0`.

Interpretation:

- A single `mprotect(PROT_READ)` already shows the `v6.16 -> v6.17` slowdown.
- The main report is therefore not only measuring repeated permission
  toggling.
- This still points at the same `mprotect()` PTE update path, so it should be
  used as supporting follow-up evidence, not as a separate report.

## Files

- `single_protect_reproducer.c`: standalone reproducer for this follow-up.
- `run_single_protect_reproducer.sh`: local helper script.
- `step-summary.csv`: per boot/run step summary.
- `aggregate-summary.csv`: per-kernel aggregate summary.
