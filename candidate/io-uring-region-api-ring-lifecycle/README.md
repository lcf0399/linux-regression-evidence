# io_uring standard-ring lifecycle cost after region API conversions

This bundle records a narrow io_uring performance change: repeatedly creating
and destroying a standard ring is `10.832%` slower on v7.1.3 than on v6.12.95.
Two direct-parent bare-metal tests further attribute `4.563%` and `3.019%`
increases in the complete lifecycle cost to the adjacent SQ and CQ region API
conversions.

Upstream state: **not sent**. A current duplicate/fix audit and recipient check
will be performed if a send decision is made.

## Primary results

The release screen used a fresh-boot
`v6.12.95 A -> v7.1.3 -> v6.12.95 B` sequence:

| old A | new | old B | new vs old midpoint | drop first | old drift | max CV |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8299.366 | 9121.969 | 8161.576 | `+10.832%` | `+10.761%` | `-1.660%` | `2.171%` |

Units are ns/lifecycle. The exact source comparisons were:

| conversion | direct parent -> child | parent A | child | parent B | child vs midpoint |
| --- | --- | ---: | ---: | ---: | ---: |
| SQ | `02255d55260a` -> `8078486e1d53` | 8447.193 | 8874.057 | 8526.431 | `+4.563%` |
| CQ | `8078486e1d53` -> `81a4058e0cd0` | 8851.177 | 9155.872 | 8924.005 | `+3.019%` |

Multiplying the adjacent ratios gives `+7.719%`; comparing the first-parent
midpoint with the final child gives `+7.884%`. These commits explain most, but
not all, of the release signal. The release and exact-source tests use
different source baselines, so their percentages must not be mechanically
subtracted to claim an exact residual.

## Workload

The exact `L0_STANDARD_LIFECYCLE` workload uses the raw UAPI without liburing.
Every timed event:

1. creates a 64-entry standard ring with `io_uring_setup()`;
2. maps the standard ring and SQE regions;
3. submits and verifies one `IORING_OP_NOP`;
4. unmaps the regions and closes the ring fd.

Each point has 3 warm-up rounds and 15 measured rounds, with 512 lifecycles per
round. A pause used to allow asynchronous cleanup occurs outside timing. The
metric covers the syscall-visible setup/mmap/NOP/munmap/close lifecycle; it is
not the isolated runtime of `io_create_region()`, `io_free_region()`, or one
`memmap.c` helper.

## Scope

- This is a legal, reproducible short-lifecycle synthetic microbenchmark, not
  an application benchmark.
- Applications that create one ring and reuse it do not repeatedly pay this
  setup/teardown cost.
- Both commits modify multiple io_uring files, so attribution stops at the
  whole-commit boundary.
- The evidence does not request reverting the region API conversions. It asks
  whether standard-ring setup and teardown can retain a lighter path while
  preserving the region API's lifecycle-management benefits.

## Contents

- [`bare-metal/`](bare-metal/) contains the release screen, both exact commit
  pairs, selected round data, identities, and direct-hit summary;
- [`reproducer/`](reproducer/) contains the exact measured raw-UAPI workload,
  runner, contract, and validator.
