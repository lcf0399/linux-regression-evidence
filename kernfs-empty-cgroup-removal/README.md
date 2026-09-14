# kernfs: empty cgroup removal cost

Original report sent on 2026-09-13. A separate INODE_INITED patch validation
was completed on 2026-09-14; see the follow-up below.
This bundle is a scoped cost report, not a proposed revert or a claim that all
of the added work is avoidable.

Two independent bare-metal runs found that removing an empty cgroup v2 leaf
became approximately 46–48% slower across two consecutive kernfs changes:

- `507d8ce13f5b91d5b4dca7bd4b4e4249e8021cca`: protect the inode link count
  while a directory is being removed;
- `eea5d2bb34ba11dccd9c53f392dc50cf060150a9`: support deletion notifications
  and automatic inotify watch cleanup.

These changes have a correctness purpose. Their measured deletion cost and
the possibility of reducing that cost are separate questions. The experiment
originated in sched_ext cgroup testing, but the same increase occurs with
sched_ext disabled; it is not an ext.c-specific regression.

Second exact run, sched_ext disabled, medians of nine sample means per boot:

| State | boot A, µs/rmdir | boot B, µs/rmdir |
| --- | ---: | ---: |
| Before both changes, `f917dc56060a` | 6.504 | 6.528 |
| Plus link-count protection, `507d8ce13f5b` | 6.956 | 6.922 |
| Plus deletion notifications, `eea5d2bb34ba` | 9.553 | 9.558 |

This is about 3 µs more per removal, not a 46% increase in application or
container-teardown time. The leaf has no tasks or child groups. Its 25 control
files are kernel-created, so one removal processes 26 kernfs nodes. Creation
and deletion are timed separately; state checks are outside timing.

## Separate follow-ups

| Experiment | Finding | Limit |
| --- | --- | --- |
| Exact changes, two independent six-boot runs | All four deletion conditions pass the 3% CV, 2% drift and 5% effect checks, including drop-first | Confirms added cost, not that correctness can be preserved at the old cost |
| Local root-reuse prototype, one four-boot run | About 4–5% lower deletion latency while retaining locks, lookups and notifications | Not consistently at least 5%; concurrency validation is incomplete; not a submitted fix |
| v7.2 → pinned 7.3-rc2 → v7.2 | 9.919 / 9.719 / 9.953 µs; 2.019% / 2.355% improvement | One separate version comparison; not a new measurement of the exact 46–48% gap |
| [INODE_INITED patch validation](bare-metal/inode-inited/README.md), two distinct 128-operation four-boot experiments | Original binary: rmdir 21.87–22.86% faster. Stage follow-up: continuous rmdir 22.17–22.37% faster | These continuous overall timings remain noisy; paced overall improvement 1.38–2.72%. Basic semantics passed, not exhaustive concurrency safety or a complete fix |
| [INODE_INITED pacing diagnostic](bare-metal/inode-inited/pacing/README.md), separate 32-operation four-boot matrix | With 16 warmups, continuous full sequence 5.77–8.76% shorter; maximum CV 1.76%, boot drift below 2%. All six conditions retain 2.10–2.58 µs removal savings | Earlier noisy data and no-warmup conditions retained; timing sensitivity, not application benefit or a complete fix |

Creation results and unstable secondary observations are retained in the data.
The samples from these experiments are never pooled.

## Evidence

- [Bare-metal results and verification](bare-metal/README.md): 603 invocation
  samples, exact build/runtime identities and separate non-timing evidence.
- [Tested control source and reproduction](reproducer/README.md): the unchanged
  control used with sched_ext disabled, with its required headers.
- [Optimization prototype and limits](attribution/README.md).
- [Dated upstream audit and report status](upstream-status/README.md).
- [中文说明](README.zh-CN.md).

The report asks whether per-node deletion work can be reduced while retaining
the notification and race fixes. No measured application impact or complete
safe fix is claimed.
