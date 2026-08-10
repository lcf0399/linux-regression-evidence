# Bare-metal results

## Release screen

The fresh-boot order was
`v6.12.95 A -> v7.1.3 -> v6.12.95 B`. Only
`L0_STANDARD_LIFECYCLE` passed the old-faster, drop-first, control-drift, and
CV gates:

| old A | v7.1.3 | old B | new vs midpoint | drop first | old drift | max CV |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8299.366 | 9121.969 | 8161.576 | `+10.832%` | `+10.761%` | `-1.660%` | `2.171%` |

The complete four-profile screen remains in
[`release-summary.tsv`](release-summary.tsv). R0, N0, and A0 are preserved as
negative results or an environment control and are not part of this claim.

## Exact commit pairs

Both comparisons used a fresh-boot `parent A -> child -> parent B` order:

| pair | parent -> child | parent A | child | parent B | delta | drop first | drift | max CV |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SQ | `02255d55260a` -> `8078486e1d53` | 8447.193 | 8874.057 | 8526.431 | `+4.563%` | `+4.627%` | `+0.938%` | `2.061%` |
| CQ | `8078486e1d53` -> `81a4058e0cd0` | 8851.177 | 9155.872 | 8924.005 | `+3.019%` | `+2.909%` | `+0.823%` | `2.362%` |

Commit `8078486e1d53` is the SQ child and the CQ parent. Its means from the two
independent boot sequences differ by only `0.153%`, providing an internal
consistency check. All 90 exact-pair measured rows and all 45 release L0 rows
passed the setup, mmap, NOP, munmap, close, ring-health, CPU, and semantic
checks.

## Environment and identity

- Intel Core i7-12700KF, 32 GiB RAM, pinned to P-core logical CPU 2;
- governor and EPP set to `performance`, Turbo disabled, tracing off for timing;
- 3 warm-up and 15 measured rounds per point, 512 lifecycles per round;
- requested/build/actual preemption was `none/dynamic/full` at every point, so
  the actual runtime mode was matched at `full`;
- exact pairs used the same canonical config, GCC 15.2.0, Kbuild metadata,
  equal-length release strings, and identical workload binary;
- all six exact-pair boot IDs and runtime Build IDs were independent and
  matched their expected artifacts.

An untimed CQ-child trace recorded 68 `io_create_region()`, 102
`io_free_region()`, and 64 `io_uring_mmap()` entries. See
[`direct-hit.tsv`](direct-hit.tsv). These counts prove the target path was hit
but are not timing evidence.

## Files

- [`release-measured-rounds.tsv`](release-measured-rounds.tsv): 45 selected
  release L0 measured rows;
- [`exact-measured-rounds.tsv`](exact-measured-rounds.tsv): 90 selected rows
  from the two exact pairs;
- [`release-run-identity.tsv`](release-run-identity.tsv) and
  [`exact-run-identity.tsv`](exact-run-identity.tsv): boot, build, preemption,
  and workload identity;
- [`source-identity.tsv`](source-identity.tsv): the three adjacent source
  points;
- [`workload-identity.tsv`](workload-identity.tsv): frozen-input hashes.
