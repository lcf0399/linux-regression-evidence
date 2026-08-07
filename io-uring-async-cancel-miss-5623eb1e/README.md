# io_uring async-cancel miss-path regression

This bundle records a narrow io_uring async-cancel slowdown introduced by
[`5623eb1ed035`](https://github.com/torvalds/linux/commit/5623eb1ed035f01dfa620366a82b667545b10c82)
(`io_uring/tctx: add separate lock for list of tctx's in ctx`).

The primary `A0_MISS` workload keeps real eventfd poll requests pending, then
submits `IORING_OP_ASYNC_CANCEL` requests whose `user_data` keys are guaranteed
not to exist. The timed region covers cancel SQE preparation, submission,
waiting, and CQE collection; `-ENOENT` is verified immediately afterward.
Poll setup and cleanup are also outside timing. A matched `A0_HIT` profile
cancels real keys as a control. The two profiles have different completion
shapes, so their absolute costs are evaluated separately and never subtracted.

In a fresh-boot direct-parent sandwich, the guaranteed-miss path was `8.833%`
slower in the child, while the matched hit-path control changed by `-0.562%`:

| profile | parent A ns/attempt | child | parent B | child vs parent midpoint |
| --- | ---: | ---: | ---: | ---: |
| `A0_MISS` | 143.660 | 156.137 | 143.271 | `+8.833%` |
| `A0_HIT` control | 131.641 | 130.324 | 130.480 | `-0.562%` |

Dropping the first measured round gave `+8.807%` for `A0_MISS`; parent drift
was `-0.271%`, and the maximum CV was `0.422%`. All 90 measured rows passed the
semantic checks. Every compared kernel actually ran with `preempt=full` and
used the same normalized config and workload binary.

A separate run of the shorter 373-line standalone reproducer found `+9.099%`
for the same miss path, corroborating the exact-source result. The formal
claim remains bound to the original 1,341-line measured source.

The commit changes tctx-list locking and traversal as well as task-state
handling. The evidence therefore identifies the whole exact commit, not one
mutex operation, and does not recommend reverting the correctness change.
This is a focused synthetic microbenchmark, not an application benchmark or a
claim that successful, bulk, synchronous, or teardown cancellation regressed.

## Contents

- [`bare-metal/`](bare-metal/) contains the exact A/B result, selected measured
  rows, build identity, and untimed direct-hit counts;
- [`reproducer/`](reproducer/) contains both the exact measured raw-UAPI
  source and a shorter commented standalone reproducer, plus the validator;
- [`upstream-status/`](upstream-status/) records the introducing thread and the
  dated duplicate/fix audit.
