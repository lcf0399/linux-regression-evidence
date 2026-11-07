# Upstream discussion and follow-up

Updated November 7, 2026. Dates below use China time (UTC+08:00).
Mailbox-confirmed replies and sent follow-ups are dated records, not a
claim that every message has a verified public archive URL.

## Current disposition: necessary accounting, measured workload cost

Vincent Guittot's November 7 reply explains why `333238da9a19` is needed.
Resetting the wakee's lag before charging current's outstanding runtime
can give the wakee undeserved positive lag and allow it to preempt.
Updating current first removes that advantage; slice protection can then
let current continue.

Turning DELAY_ZERO off can retain positive credit accumulated during
delayed dequeue. It restores throughput in this test but can treat zero
lag and a tiny negative lag very differently on wakeup. It is not an
established fair replacement policy. The measured loss remains valid;
removing the accounting update or changing the global default is not
the proposed resolution.

No new fix was supplied with that explanation. Further experiments and
thanks-only replies are paused. A concrete upstream change or a new
testable result can justify another bounded comparison.

## Existing thread and report

- Thread: `Re: [PATCH v3 7/7] sched/eevdf: Move to a single runqueue`.
- [Mike's October 18 report](https://lists.openwall.net/linux-kernel/2026/10/18/311)
  already identified `333238da9a193ffc58792995f3e951e4cb87bfd2`,
  “sched: Update time before requeueing delayed entities,” in a wakeup-jitter test.
- [Vincent's October 21 explanation](https://lkml.iu.edu/2610.2/13555.html)
  covers lag, slice protection and a separate flat-hierarchy wakeup case.
- [Our October 30 report](https://lists.openwall.net/linux-kernel/2026/10/30/1506)
  supplies native pipe-throughput, scaling and matched-revert evidence.
  Its missing reply headers were followed by a short threaded link reply;
  the report and attachment were not resent.

The periodic wakeup task with CPU-bound competition is not the same
workload as the pipe participants. A shared accounting change does not
by itself establish an identical detailed scheduling sequence.

## November 1: bounded redundant-update prototype

The matched v7.3 baseline / prototype / baseline test has 54 clean calls.
The prototype skips the leading `update_curr_eevdf()` in
`__dequeue_task()` only when `pick_task_fair()` has already updated
current. Ordinary dequeue, wakeup-preemption, lag and hierarchy accounting
remain intact; it does not revert `333238da9a19`.

Throughput changes by +1.27% at two workers and −0.22% at 15 workers.
Separate probes observe only about 1.4 such leading updates per million
handoffs at 15 workers. This is not the frequent delayed-requeue path and
does not recover the main loss. The result was sent on November 1.
Mike subsequently clarified that his setup used pinned wakelat and eight
CPU-bound tasks in the same cgroup; no equivalence to this prototype is assumed.

## November 3: DELAY_ZERO control

Tianchen Ding reported a separate pipe regression bisected to the same
commit. His proposed explanation is a change from wakeup preemption to
current continuing until a pipe read blocks: the updated accounting
changes lag, eligibility and repositioning under DELAY_ZERO. He asked
for the feature-toggle comparison.

The same unpatched v7.3 image, original two/15-worker thread conditions
and native P-core CORE masks were used. Three fresh boots, nine clean
calls per condition/boot, give 54 calls. Correct accounting and
DELAY_DEQUEUE remain enabled.

| Workers | On A, handoffs/s | Off, handoffs/s | On B, handoffs/s | Off vs. on mean | Return drift |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | 1,156,118 | 1,158,966 | 1,157,812 | +0.173% | +0.146% |
| 15 | 9,172,415 | 10,424,959 | 9,164,990 | +13.702% | −0.081% |

Maximum within-boot CV is 0.603% overall and 0.134% at 15 workers.
Independent replay covered 810 measured intervals and recovery checks.
The result was sent in reply to Tianchen on November 3.

Separate full-invocation counters, including warmup, show the 15-worker
involuntary-switch share changing from 0.357% to 41.482% and back to
0.359%. This supports a change in handoff behavior; it does not reconstruct
every wakeup or establish the fairness of disabling the feature.

## Dated source audit, not a current merge claim

The October 30 source comparison used tested v7.3
`c03c1a14507dfaf8b856c2608f9ac216021fc222`:

| Branch | Checked tip | Compared paths |
| --- | --- | --- |
| sched/core | [`b99b7f60c5b4`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/tip/tip/+/b99b7f60c5b473d374c68b53753153b4aa9e0702/kernel/sched/fair.c) | Requeue and hierarchy functions unchanged; enqueue difference was comment-adjacent |
| sched/urgent | [`cac037a85c19`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/tip/tip/+/cac037a85c1971ec75c4d3edf5a28af9ec2a089c/kernel/sched/fair.c) | All three compared functions unchanged |

These tips were not benchmarked. The checked scope did not identify a
matching repair; it is not a fresh audit of current maintainer trees.
Complete correspondence and delivery verification remain local.
