# Public tracking status: 2026-10-19

This audit covers regzbot's [All view](https://linux-regtracking.leemhuis.info/regzbot/all/)
and the individual entries, generated on October 19 at 15:12:10 UTC+08:00.
Dates below use UTC+08:00. Matching the reporter's name and known email identities
found eight entries, not eight independent new issues. This updates tracking
status; it is neither a new benchmark nor a refresh of every earlier source audit.

## Tracking closure is not necessarily a fix

`marked resolved` can mean a manual decision to stop tracking. In particular,
`inconclusive` does not establish a technical resolution or a merged fix.

| Report | Tracking record and date | Interpretation |
| --- | --- | --- |
| [Shared-dirty mprotect](../mm/mprotect-shared-dirty-toggle/) | [June report](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/7c9eeb21.1c04f.19efaacbb2f.Coremail.chengfenglin@stu.xmu.edu.cn/): manually closed October 12, inconclusive | The tracker cites others' inability to reproduce and a possible microbenchmark issue. This is not a fix or a remeasurement of the local evidence. |
| [Concurrent fsnotify watch updates](../fs/fsnotify-concurrent-inotify-watch-setup/) | [Entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgd=RwMy=29OYG14b6kkoF1uBHz67jTGge5Nubb-XemMvBg@mail.gmail.com/): manually closed October 12 | Jan Kara considers the scalability trade-off acceptable while planning improvements; not a completed fix. |
| [Accounted WAITV allocation](../io_uring/io-uring-futex-waitv-accounted-allocation/) | [Entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/20260830190717.1509012-1-lin2530632123@gmail.com/): manually closed October 13, inconclusive | Lack of feedback and likely microbenchmark relevance were the reasons to stop tracking, not grounds to remove necessary accounting. |
| [MSG_RING SEND_FD installation](../io_uring/io-uring-msg-ring-send-fd-install/) | [Entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgdmt0FQ=offsdfn+wEaDxbOFoAa6bi92X_vEo4S6aCZ56A@mail.gmail.com/): manually closed October 13, inconclusive | The tracker cites a microbenchmark issue already being investigated. An RFC and review exist; acceptance is not established. |
| [AppArmor AF_UNIX send](../security/apparmor-af-unix-send-old-abi-6456cc/) | [Entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgdnoz4NdqzOV9+SZaYTc+O_GkNNn5NVp+ZpHUQbp5h-QVQ@mail.gmail.com/): manually closed October 13, inconclusive | Similar tracking rationale. Public maintainer evidence is a commitment to investigate, not a completed implementation. |
| [Scalar futex wait/wake](../io_uring/io-uring-futex-inflight-wait-wake/) | [Entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/CANGjgdn=R_qyUdE=j9za+vkmqcxacbP-84OHXF4nZ4ho9qRyVg@mail.gmail.com/) links `73e701909747` and `4d327bbd1cd2`, mainline v7.3-rc1 and stable backports | This entry has actual corresponding fix commits. Original measurement limits remain; it does not establish elimination of every historical cost difference. |
| May mprotect report | [Older entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/662955ba.f499.19e3b2cf478.Coremail.23020251154299@stu.xmu.edu.cn/): no closure marker; last activity May 26 | Separately registered from the June report, not a new independent issue or evidence of recent active work. |
| Early PAGEOUT/THP/no-swap report | [Older entry](https://linux-regtracking.leemhuis.info/regzbot/regression/lore/3f4567fc.f496.19e3b2b297b.Coremail.23020251154299@stu.xmu.edu.cn/): manually closed June 3 | The reporter no longer considered it a performance regression; not a patch-based resolution. |

The October 12 closures link to [one tracker message](https://lore.kernel.org/regressions/6a9e41a6-5acc-4e7b-8516-8c78935e5f6e@leemhuis.info/),
and the October 13 closures to [another](https://lore.kernel.org/regressions/45ee647a-6e60-42c2-a165-dc21258f8999@leemhuis.info/).
The reasons above were read directly from the individual regzbot entries.
Access to those two underlying messages was restricted during this audit;
no additional reasoning is attributed to them beyond the tracker records.

## What the developer activity actually establishes

### MSG_RING: implementation and review, unresolved trade-offs

Uzair Beg's [September 14 three-patch RFC cover](https://lists.openwall.net/linux-kernel/2026/09/14/1077)
explicitly references this report. It proposes a dedicated resource-node slab,
bulk refill on a cache miss, and prefill at sparse-table registration.
The cover attributes its measurements to Chengfeng; they are not another
developer's independent reproduction.

In his [September 26 review](https://lists.openwall.net/linux-kernel/2026/09/25/2034),
Gabriel Krisman Bertazi objects to generally enlarging a cache whose nodes are
not reclaimable. He asks for realistic working sets exceeding 128 nodes and
whether subsequent I/O dilutes installation cost. Conditional on a real need,
he would consider bulk refill with the capacity kept at 128, preferably without
a dedicated slab. This is substantive review, not acceptance or proof that
development remains active today.

### AppArmor: commitment to investigate; our later prototype is separate

John Johansen's [August 6 response](https://lists.ubuntu.com/archives/apparmor/2026-August/014884.html)
says the overhead needs investigation, notes an apparent existing early bailout,
and expects room for improvement without promising full recovery. It establishes
maintainer engagement, not a completed analysis or a published fix.

The [September 23 prototype follow-up](https://lists.openwall.net/linux-kernel/2026/09/22/2475)
is by Chengfeng, including bounded correctness checks and a cost on another path.
The public thread checked here shows no later maintainer technical conclusion.
This research prototype must not be attributed to the maintainer.

## Cases not covered by these closures

It would be inaccurate to say that all reports except the two recent submissions
have received a final upstream technical outcome.

- [tmpfs](../fs/tmpfs-flistxattr-small-list/) and
  [Btrfs](../fs/btrfs-remap-writeback-inhibition-v2/) retain their established
  mainline-fix evidence regardless of an unmatched tracker entry.
- [kernfs](../fs/kernfs-empty-cgroup-removal/) and
  [RT sysctl](../kernel/sched/sched-rt-sysctl-read-rebuild/) have no independent
  entry matched in this audit. Existing responses and patch tests are not final
  acceptance or rejection; their individually dated records remain authoritative.
- No entry was matched for the [fair default-slice](../kernel/sched/sched-fair-default-slice-noop/)
  or [swap-fallback ordering](../mm/mm-swap-fallback-folio-order/) submissions
  either; their submission, review and merge states belong to their own threads.

Regzbot is not a complete index of all mail and patches. Tracking disposition,
maintainer conclusions, merged changes and measured results remain separate.
