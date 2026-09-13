# kernfs: empty cgroup removal cost

Prepared for upstream submission on 2026-09-13; the report has not been sent.
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
