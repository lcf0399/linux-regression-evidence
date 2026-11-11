# Linux Regression Evidence

This repository contains curated public evidence for focused Linux performance
regressions, upstream follow-up, and patch validation.

Upstream-status statements are dated snapshots, with supporting links in
the individual bundles.

The [2026-10-19 public tracking update](upstream-status/README.md) records five
manual closures, separating fixes, accepted trade-offs, inconclusive outcomes
and an older unclosed entry. Manual closure does not increase the fix count.

## Recent update — November 11, 2026

The November 11 refresh concerns zswap; other notes retain their November 7 snapshot.

- Three supplementary MMTests reports now have subsystem homes: pipe handoff,
  Redis post-flush cost and the sembench balancing tradeoff. They are not
  discoveries of the model-generated workload method or new issues by directory count.
- Pipe handoff includes the DELAY_ZERO control and upstream fairness explanation;
  Redis includes microcode and direct-commit CPU profiles; sembench retains the
  small v4 improvement. Replies, verified fixes and merges are distinct states.
- Zswap supplementary results were sent on November 11 and moved to `mm/`.
  A separate eight-worker, 256 MiB batch supports the finite-limit benefit;
  return drift limits effect-size precision. Historical noisy 32 MiB batches remain
  separate. Ordinary NOP and time-unmap retain scoped cost findings, not complete attribution of version gaps.
- New experiments use 7.3 as the latest endpoint; completed 7.2 evidence keeps
  its original identity. One libMicro sweep produced 1,092 valid results and
  33 latest-end lead conditions, not 33 confirmed regressions. The native
  network matrix is still running. Full exploratory archives, unaccepted
  outputs and execution queues are not part of this public repository.

## Browse by subsystem

Sixteen evidence bundles are grouped by kernel subsystem.

| Directory | Cases | Topics |
| --- | ---: | --- |
| [fs/](fs/) | 4 | Btrfs, fsnotify, kernfs, tmpfs xattr |
| [mm/](mm/) | 3 | mprotect; swap-fallback folio order; finite zswap limit checks |
| [io_uring/](io_uring/) | 3 | MSG_RING SEND_FD, scalar futex, WAITV |
| [kernel/sched/](kernel/sched/) | 4 | Fair default slice, delayed-requeue pipe handoff, sembench new-idle tradeoff, RT sysctl |
| [security/](security/) | 1 | AppArmor AF_UNIX |
| [arch/x86/](arch/x86/) | 1 | Persistent post-flush cost in Redis |

Six standalone candidate bundles and an additional review inventory are listed
under [candidate/](candidate/).

## Sent or active upstream threads

Source/merge snapshot: 2026-10-10; regzbot tracking was separately updated on
2026-10-19. Mainline source reference:
[`3857c2fe5449`, v7.3-rc6](https://github.com/torvalds/linux/tree/3857c2fe5449541e24afc5efdb0f81a8a8f9a3a0).
The 11 reported issue families below have bare-metal evidence.
Three families have corresponding mainline fixes; fsnotify is closed with the
maintainer's trade-off accepted. Other tracker closures are not confirmed fixes,
and not every older report has a final upstream technical outcome. The tmpfs, Btrfs,
scalar-futex and fsnotify investigations are all closed as of 2026-10-10, with no
further experiments or follow-up queued. Their evidence is retained. The tmpfs fix
entered mainline on June 15, before our July 6 report; the tested 7.1 releases
lacked it. The fix is included in the v7.2 final release (August 17, UTC+08:00).
This is not a claim that three patches authored here were merged.
Dates below use UTC+08:00. The tmpfs v7.2 release inclusion is confirmed;
older stable backports are not covered here.

| Evidence | Upstream state | Current technical state |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mm/mprotect-shared-dirty-toggle/) | Regzbot closed it as inconclusive on October 12, citing others' inability to reproduce and a possible microbenchmark issue. [Tracking details](upstream-status/README.md). Not a confirmed fix. | Exact A/B attributes a `39.77%` slowdown to `cac1db8c3aad`. Nested diagnostics recover `87.29%` of the gap from generic single-PTE update/flush handling and the normal page/folio lookup. Pedro v3 did not improve this workload; no safe fix is proposed. |
| [`tmpfs-flistxattr-small-list/`](fs/tmpfs-flistxattr-small-list/) | The existing upstream fix [`1e7cd8a53b72`](https://github.com/torvalds/linux/commit/1e7cd8a53b72) first shipped in the v7.2 mainline final release. Our July 17 reply validated the then-existing mainline fix. This is not a patch authored here. | The per-superblock cache commit removes the measured small-list tmpfs slowdown. Closed; evidence retained. |
| [`fsnotify-concurrent-inotify-watch-setup/`](fs/fsnotify-concurrent-inotify-watch-setup/) | Regzbot closed it on October 12 because Jan Kara accepts the scalability trade-off while planning improvements; not a patch-based fix. [Details](fs/fsnotify-concurrent-inotify-watch-setup/upstream-status/README.md). | P1/P4 are below the signal gate; P6/P8 show a parallel scalability loss on the superblock connector list. Upstream considers the current trade-off acceptable and mentioned unfinished rhashtable work that may improve it. |
| [`btrfs-remap-writeback-inhibition-v2/`](fs/btrfs-remap-writeback-inhibition-v2/) | Beyond `for-next`: [`67bd829a14b7`](https://github.com/torvalds/linux/commit/67bd829a14b7d71ffea28147b98dadf9ee684568) is in mainline and includes our `Tested-by`. A September 12 v7.2 stable-review mail also names that upstream commit; this audit does not establish the first released stable version containing it. | Independent patch validation found about `27%` lower clone cost and `22%` lower dedupe cost for the included 4 KiB micro-workload. This is patch validation, not a broad Btrfs claim. |
| [`apparmor-af-unix-send-old-abi-6456cc/`](security/apparmor-af-unix-send-old-abi-6456cc/) | Regzbot closed it as inconclusive on October 13, citing microbenchmark relevance and developer attention. John's public response commits to investigation; the September prototype is ours, without a confirmed later maintainer conclusion. [Details](security/apparmor-af-unix-send-old-abi-6456cc/upstream-status/README.md). | The exact source delta makes unconfined AF_UNIX datagram `sendmmsg()` `15.295%` slower; an independent larger runner reproduces the direction. The report asks to retain the ABI correctness fix while reducing the added send-path cost. |
| [`io-uring-msg-ring-send-fd-install/`](io_uring/io-uring-msg-ring-send-fd-install/) | Regzbot closed it as inconclusive on October 13, citing microbenchmark relevance and existing developer work. Uzair's three-patch RFC and Gabriel's review are concrete activity, not acceptance or merge; memory and real-workload trade-offs remain open. [Details](io_uring/io-uring-msg-ring-send-fd-install/upstream-status/README.md). | Original exact installation slowdown remains `11.621%`. New stable 64/256-connection TCP totals changed by less than 1%. Old dedicated-slab/32 and independent shared-kmalloc/128 tests showed no clear benefit; the latter slowed 64-entry registration plus first fill by `9.61%`. Results are [grouped by test](io_uring/io-uring-msg-ring-send-fd-install/bare-metal/README.md), with instability and historical v3 memory costs retained. |
| [`io-uring-futex-inflight-wait-wake/`](io_uring/io-uring-futex-inflight-wait-wake/) | Mainline fix [`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef) carries our `Reported-by`. Regzbot links it and `4d327bbd1cd2`, listing v7.3-rc1 and stable backports. [Record](io_uring/io-uring-futex-inflight-wait-wake/upstream-status/README.md). This is not an inconclusive closure. | Direct-parent A/B remains `+9.268%`. Patch 1 improved the newer-master private workload by `3.392%`; patch 2 was private-neutral and improved the matched shared workload by `1.578%`. The unnecessary synchronous-WAKE tracking is fixed, so this line is closed unless release validation fails. |
| [`io-uring-futex-waitv-accounted-allocation/`](io_uring/io-uring-futex-waitv-accounted-allocation/) | Regzbot closed it as inconclusive on October 13 for lack of feedback and likely microbenchmark relevance, not a performance fix. [Details](io_uring/io-uring-futex-waitv-accounted-allocation/upstream-status/README.md). Merging the original accounting change did not resolve the cost report. | Exact direct-parent A/B is `+8.091%`, a separate standalone is `+6.488%`, and the scalar control is `-0.555%`. A separate v7.2 diagnostic still isolates about `8–9%` to the accounted WAITV allocation without proposing removal of memcg accounting. |

## Scoped cost report and patch follow-up

This group's upstream checks began on 2026-10-10; the default-slice row also records the October 13 reply and October 19 formal submission (UTC+08:00). Merge observations retain their individual check dates; this update is not a fresh audit of every maintainer tree.

| Evidence | Report state | Current technical state |
| --- | --- | --- |
| [`kernfs-empty-cgroup-removal/`](fs/kernfs-empty-cgroup-removal/) | T.J. Mercier's latest reply is September 16. Our independent-marker prototype and patch were sent September 17; no later reply was found. The checked mainline does not contain that shortcut. | Exact-source deletion increase about 46–48%. The new [inode-request marker](fs/kernfs-empty-cgroup-removal/bare-metal/inode-requested/README.md) reduces rmdir latency 22.36–22.72% and continuous whole-sequence latency 7.35–9.01% in one matrix, at 8 extra bytes per node. Old experiments remain separate; unresolved correctness coverage and no measured application impact. |
| [`fair unchanged default slice`](kernel/sched/sched-fair-default-slice-noop/) | Peter said the October 13 revision looks reasonable. The same code was submitted as a [formal patch](https://lists.openwall.net/linux-kernel/2026/10/18/950) on October 19 UTC+08:00 and has a public archive record. Neither approval in ordinary prose nor submission establishes Reviewed-by or a merge; merge status was not refreshed here. | The v7.2 revision handles the two reported mode/uclamp cases in the tests and reduces unchanged-default cost from about 616.51 to 162.39 ns/call. It is not a complete attribution of the earlier release slowdown. |

## Sent supplementary reports

| Evidence | Report state | Core evidence |
| --- | --- | --- |
| [`MMTests pipe handoff`](kernel/sched/sched-fair-delayed-requeue-handoff-333238da/) | Reported October 30; DELAY_ZERO results sent November 3; fairness explanation received November 7. Further experiments paused. Native MMTests/will-it-scale, not generated workloads. | At 15 workers, v7.3 thread/process throughput is 15.69%/14.82% below v7.1.3. Disabling DELAY_ZERO improves throughput 13.70%, but is not a validated fair production fix. |
| [`Redis post-IBPB cost`](arch/x86/redis-ibpb-post-flush-a3af84b0/) | Original report sent November 3; profiles and microcode reply sent November 4. Bounded follow-up closed; exact archive URLs remain unchecked. Native MMTests Redis. | v7.3 ZPOPMIN throughput −12.87%, full-test time +0.58%. Both microcodes reproduce; added CPU samples are distributed across kernel send/receive paths. Persistent processor-state mechanism remains unexplained. |
| [`sembench new-idle tradeoff`](kernel/sched/sched-newidle-sembench-tradeoff/) | User confirmed sending on November 5 as feedback on the posted Bresenham v4. Sent headers and public archival have not been independently checked. Found through native MMTests/sembench, not generated workloads. | Disabling NI_RANDOM on v7.3 raises 815-worker mean throughput by 23.48%; matched v4 improves it only 1.98%, without recovering the main loss. The toggle is a diagnostic control, not a production recommendation. |
| [`finite zswap limit statistics flush`](mm/mm-zswap-limit-rstat-flush/) | Supplement sent to the original thread on November 11; Gmail Sent body, reply headers and attachment metadata verified. Public archival remains unconfirmed. | Exact commit: single-worker program CPU +15.56%; separate MM derivative pair: −21.78%. Separate eight-worker 256 MiB supplement supports the finite-limit benefit; drift limits precise magnitude, and production impact remains unknown. |

## Optimization prototype

| Evidence | Current state | Core evidence |
| --- | --- | --- |
| [`swap-fallback folio order`](mm/mm-swap-fallback-folio-order/) | Reclaim-local ordering prototype; applicability beyond the tested backend remains open. | Fixed mm-stable pair: initial-THP PAGEOUT + cached-refault worker CPU -14.84%/-13.14%, saving 565/521 us per 8 MiB; not an application gain. |

## Additional candidates

Six unsent standalone performance or optimization-discussion bundles are grouped under
[`candidate/`](candidate/). An [additional review inventory](candidate/additional-review/README.md)
collects 22 leads with target-version and evidence limitations; its entry count
is not a count of independent established bugs.

| Candidate | Current disposition | Core evidence |
| --- | --- | --- |
| [`ordinary NOP mixed-CQE cost`](candidate/io-uring-mixed-cqe-nop-e26dca67/) | Exact shared-path attribution and bilingual report complete; not sent. | Native 20-thread throughput falls 5.564% across e26dca67fde1, with −0.0324% parent return drift. One identified source, not the entire cross-version loss. |
| [`COLD deactivation tradeoff`](candidate/mm-madv-cold-deactivation-tradeoff/) | State-dependent cost and benefit of an existing optimization. | Exact commit: per 16 MiB, fresh-file CPU +51 µs / +13.20%; oldest-file CPU −247 µs / −68.30%. Application impact is unknown; not a proposed revert. |
| [`shared swap-count cleanup`](candidate/mm-swap-count-batch-cleanup/) | Locked-batch cleanup prototype. | Fixed MM-tree pair: 3.264 -> 2.337 us/16-page munmap, about 0.927 us saved; slot-position-dependent mean, overlapping distributions, no established application gain. |
| [`lazyfree PTE-batch failure`](candidate/mm-lazyfree-dirty-pte-batch-354dffd2/) | Local optimization evidence; no application benefit established. | Same-v7.2 prototype reduces PAGEOUT by 71.37% (60.69 us/32 MiB); lifecycle means change by -1.53%/-2.26%, comparable to drift and variation. |
| [`async cancel miss`](candidate/io-uring-async-cancel-miss-5623eb1e/) | Reportable, but scoped to the guaranteed-miss slow path and the whole commit. | Exact A/B `+8.833%`, hit control `-0.562%`. |
| [`region API ring lifecycle`](candidate/io-uring-region-api-ring-lifecycle/) | Prepared; real-world impact is limited to repeated short-lived ring setup and teardown. | Release `+10.832%`; adjacent direct-parent pairs `+4.563%` and `+3.019%`. |

## Existing-patch validation

Status checked on 2026-10-10; the earlier validation measurements are unchanged.

- [RT sysctl read rebuild guard](kernel/sched/sched-rt-sysctl-read-rebuild/):
  Joseph Salisbury's existing proposal, context-adapted to v7.2, reduces the
  tested RT read-loop cost by 96.9%. Successful writes retain maintenance;
  limited correctness checks passed. Feedback was sent September 24. The checked mainline still rebuilds
  after reads and failed writes. This is validation of Joseph's proposal, not our new fix.

## Reading a bundle

Each directory is the authoritative source for its claim. A typical bundle
contains:

- `README.md` / `README.zh-CN.md`: scope, result, and caveats;
- `bare-metal/`: compact selected measurements and source/run identity;
- `reproducer/`: standalone or exact formal workload source;
- `upstream-status/`: dated duplicate/fix audit and thread status, when present.

The root index intentionally does not repeat the full experiment narrative.
Use the target README and its compact tables for exact methodology and numbers.

Local mail files follow one layout: `email/` holds bodies, translations, complete
messages and attachments; `mail-tools/` holds preparation, verification and sending
tools; received mail and delivery checks live in `private-provenance/mail/`.
These local directories are excluded from public exports. Remove temporary send
state after confirmed success; check uncertain attempts before any retry.

## Evidence policy

- Keep curated summaries, standalone reproducers, compact CSV/TSV/JSON data,
  and small attribution probes needed to understand a claim.
- State workload scope and caveats directly; do not turn a narrow
  source-calibrated result into a generic subsystem claim.
