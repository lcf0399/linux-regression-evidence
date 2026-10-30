# Upstream check — 2026-10-30

## Existing discussion

[Mike Galbraith's October 18 report](https://lists.openwall.net/linux-kernel/2026/10/18/311)
identifies `333238da9a193ffc58792995f3e951e4cb87bfd2`
("sched: Update time before requeueing delayed entities") in a wakeup-jitter
test. Our MMTests pipe-throughput result supplies complementary evidence
against the same commit, not a first report or a separately counted issue.

- Thread: `Re: [PATCH v3 7/7] sched/eevdf: Move to a single runqueue`
- Reply target: `<b1002150c3e4f877d04790cd8e85d2149d17722f.camel@gmx.de>`
- [Vincent's October 21 explanation](https://lkml.iu.edu/2610.2/13555.html)
  discusses lag, slice protection and the separate flat-hierarchy wakeup case.
- [Mike's later diagnostic](https://lkml.iu.edu/2610.2/15077.html) and
  [Jakub's follow-up](https://lkml.iu.edu/2610.2/16079.html) do not establish a
  repair of the pipe-throughput result measured here.

Their periodic wakeup task and CPU-bound competitors are not the same workload
as repeatedly blocking pipe participants. The shared commit is established;
an identical detailed cause is not assumed. The time update is required for
correct accounting, so our revert is diagnostic, not a proposed fix.

## Maintainer-tree source check

The official tip repository refs and `kernel/sched/fair.c` were read on
October 30 and compared with tested v7.3 (`c03c1a14507dfaf8b856c2608f9ac216021fc222`).

| Branch | Frozen tip | Compared functions |
| --- | --- | --- |
| sched/core | [`b99b7f60c5b4`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/tip/tip/+/b99b7f60c5b473d374c68b53753153b4aa9e0702/kernel/sched/fair.c) | `requeue_delayed_entity` and `enqueue_hierarchy` unchanged; `enqueue_task_fair` only moves an assignment across a comment and expands the comment |
| sched/urgent | [`cac037a85c19`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/tip/tip/+/cac037a85c1971ec75c4d3edf5a28af9ec2a089c/kernel/sched/fair.c) | All three functions unchanged |

No functional change in these paths, or matching new fix, was identified in
this check. This is a source comparison, not a performance test of those tips.
The absence of an internal `update_curr()` in the current requeue function is
already true of tested v7.3; it is not a newly discovered fix. The internal-call
probe and diagnostic revert in this report explicitly compare v7.2 builds.

SHA-256 of the fetched `fair.c`: sched/core
`21495f53002f83ca61c9119fd18ce6dce03d36d9bdab1d05dbee9f0a2e6bb859`;
sched/urgent `ca68eebb8458dc43c53605c18eb278a24be7ef4c9a97412bfd6ae959a43a39f5`.
The complete lore thread and Gitiles history-range view were unavailable;
the dated replies and fixed source revisions above are the checked scope.

## Reporting state

Formal public evidence prepared on October 30; **our supplementary mail is not
yet sent**. The v7.3 maintainer lookup was repeated that day. The reply is
addressed to Vincent, Mike and Peter, with Ingo, Juri and LKML copied, preserving
Mike's original thread.

The report's current workload is the existing MMTests/will-it-scale benchmark,
not our source/call-graph or model-generated scenarios. Native CORE scaling,
the matched v7.2 revert and post-warmup diagnostics are the main evidence.
Older custom-futex and single-logical-CPU measurements remain separate.
Only reproduction instructions are planned as an initial mail attachment;
the compact measurements are in the public bundle.
