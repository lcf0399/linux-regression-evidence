# Upstream status

Updated November 5, 2026 (China time).

- The user confirmed sending the reviewed text-only report. It is feedback on
  the posted Bresenham v4 and a workload supplement for the new-idle policy
  tradeoff, not a claim that v4 introduced the historical regression.
- Intended thread: `[PATCH v4] sched/fair: Replace random newidle_balance with Bresenham accumulator`.
- Intended parent: `<20261030091946.2498870-1-wangqing7171@gmail.com>`;
  [original patch](https://lkml.iu.edu/hypermail/linux/kernel/2610.3/14236.html).
- The local outgoing Message-ID is
  `<179386941645.8.3608399700168338187.sembench-newidle-v4@gmail.com>`.
  This is the prepared identity, not an independently verified sent-header readback.
- Actual sent headers, public archival and later replies were not checked in
  this organization step. There is no new confirmation of acceptance or a fix.

The case is now under `kernel/sched/`, no longer in the unsent candidate list.
Discovery came from native MMTests/sembench, not model-generated workloads.
Reviewed mail remains local; original measurement evidence stays in its existing
experiment directories. No resend or repository publication was performed.
