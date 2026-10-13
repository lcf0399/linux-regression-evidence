# Linux Regression Evidence

This repository contains curated public evidence for focused Linux performance
regressions, upstream follow-up, and patch validation.

## Sent or active upstream threads

Status checked on 2026-10-10 against Gmail (including Spam/Trash), exact public
threads and mainline source at
[`3857c2fe5449`, v7.3-rc6](https://github.com/torvalds/linux/tree/3857c2fe5449541e24afc5efdb0f81a8a8f9a3a0).
The 11 sent issue families below have local bare-metal evidence. Early student-mail
reports without that validation are outside this refresh. The mprotect public
thread was checked live; the student mailbox itself is not connected.
Three families have corresponding mainline fixes; fsnotify is closed with the
maintainer's trade-off accepted, and seven remain unresolved. The tmpfs, Btrfs,
scalar-futex and fsnotify investigations are all closed as of 2026-10-10, with no
further experiments or follow-up queued. Their evidence is retained. The tmpfs fix
entered mainline on June 15, before our July 6 report; the tested 7.1 releases
lacked it. The fix is included in the v7.2 final release (August 17, UTC+08:00).
This is not a claim that three patches authored here were merged.
Dates below use UTC+08:00; no new performance measurements were run. The tmpfs
v7.2 release inclusion was checked separately; older stable backports were not.
No reply found means none found in these checked sources,
not proof that no private or unindexed discussion exists.

The active I/O source-file exploration phase is closed. No new I/O target or
bare-metal experiment is queued. This repository now tracks only existing
upstream threads and user-selected decisions for the unsent material below.

| Evidence | Upstream state | Current technical state |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mprotect-shared-dirty-toggle/) | The [public thread](https://lore-kernel.gnuweeb.org/regressions/7c9eeb21.1c04f.19efaacbb2f.Coremail.chengfenglin%40stu.xmu.edu.cn/t/) still ends with our July 24 follow-up. No later public reply or corresponding fix was identified. | Exact A/B attributes a `39.77%` slowdown to `cac1db8c3aad`. Nested diagnostics recover `87.29%` of the gap from generic single-PTE update/flush handling and the normal page/folio lookup. Pedro v3 did not improve this workload; no safe fix is proposed. |
| [`tmpfs-flistxattr-small-list/`](tmpfs-flistxattr-small-list/) | The existing upstream fix [`1e7cd8a53b72`](https://github.com/torvalds/linux/commit/1e7cd8a53b72) first shipped in the v7.2 mainline final release. Our July 17 reply validated the then-existing mainline fix; no newer Gmail reply was found. This is not a patch authored here. | The per-superblock cache commit removes the measured small-list tmpfs slowdown. Closed; evidence retained. |
| [`fsnotify-concurrent-inotify-watch-setup/`](fsnotify-concurrent-inotify-watch-setup/) | Jan Kara accepted the contention explanation and the trade-off. No reply after our July 29 controls was found. The checked mainline still uses the connector-list lock; no corresponding completed fix was identified. | P1/P4 are below the signal gate; P6/P8 show a parallel scalability loss on the superblock connector list. Upstream considers the current trade-off acceptable and mentioned unfinished rhashtable work that may improve it. |
| [`btrfs-remap-writeback-inhibition-v2/`](btrfs-remap-writeback-inhibition-v2/) | Beyond `for-next`: [`67bd829a14b7`](https://github.com/torvalds/linux/commit/67bd829a14b7d71ffea28147b98dadf9ee684568) is in mainline and includes our `Tested-by`. A September 12 v7.2 stable-review mail also names that upstream commit; this audit does not establish the first released stable version containing it. | Independent patch validation found about `27%` lower clone cost and `22%` lower dedupe cost for the included 4 KiB micro-workload. This is patch validation, not a broad Btrfs claim. |
| [`apparmor-af-unix-send-old-abi-6456cc/`](apparmor-af-unix-send-old-abi-6456cc/) | John Johansen replied on August 6. Our prototype follow-up was sent on September 23; no later reply was found. The checked mainline lacks the proposed early unconfined-path shortcut. | The exact source delta makes unconfined AF_UNIX datagram `sendmmsg()` `15.295%` slower; an independent larger runner reproduces the direction. The report asks to retain the ABI correctness fix while reducing the added send-path cost. |
| [`io-uring-msg-ring-send-fd-install/`](io-uring-msg-ring-send-fd-install/) | The new results were sent to Uzair on October 1, not left as a draft. No later reply was found. The latest public RFC discussion remains Gabriel's September 26 reply; no matching bulk/prefill change is in the checked mainline. | Original exact installation slowdown remains `11.621%`. New stable 64/256-connection TCP totals changed by less than 1%. Old dedicated-slab/32 and independent shared-kmalloc/128 tests showed no clear benefit; the latter slowed 64-entry registration plus first fill by `9.61%`. Results are [grouped by test](io-uring-msg-ring-send-fd-install/bare-metal/README.md), with instability and historical v3 memory costs retained. |
| [`io-uring-futex-inflight-wait-wake/`](io-uring-futex-inflight-wait-wake/) | Mainline fix [`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef) carries our `Reported-by`. Current source retains the WAKE/private-WAIT tracking changes. Stable queue/review mail was also received; release containment is a separate check. | Direct-parent A/B remains `+9.268%`. Patch 1 improved the newer-master private workload by `3.392%`; patch 2 was private-neutral and improved the matched shared workload by `1.578%`. The unnecessary synchronous-WAKE tracking is fixed, so this line is closed unless release validation fails. |
| [`io-uring-futex-waitv-accounted-allocation/`](io-uring-futex-waitv-accounted-allocation/) | Our August 31 reply has no later response in the checked mailbox. Mainline still uses `GFP_KERNEL_ACCOUNT` for WAITV. The original allocation patch being merged does not mean this separate cost report was resolved. | Exact direct-parent A/B is `+8.091%`, a separate standalone is `+6.488%`, and the scalar control is `-0.555%`. A separate v7.2 diagnostic still isolates about `8–9%` to the accounted WAITV allocation without proposing removal of memcg accounting. |

## Scoped cost report and patch follow-up

Upstream status was checked on 2026-10-10; the default-slice row also records the fourth-revision reply sent on October 13.

| Evidence | Report state | Current technical state |
| --- | --- | --- |
| [`kernfs-empty-cgroup-removal/`](kernfs-empty-cgroup-removal/) | T.J. Mercier's latest reply is September 16. Our independent-marker prototype and patch were sent September 17; no later reply was found. The checked mainline does not contain that shortcut. | Exact-source deletion increase about 46–48%. The new [inode-request marker](kernfs-empty-cgroup-removal/bare-metal/inode-requested/README.md) reduces rmdir latency 22.36–22.72% and continuous whole-sequence latency 7.35–9.01% in one matrix, at 8 extra bytes per node. Old experiments remain separate; unresolved correctness coverage and no measured application impact. |
| [`fair unchanged default slice`](sched-fair-default-slice-noop/) | RFC sent October 9, intact patch attached in an October 10 reply. Peter replied October 12; the tested fourth revision and its results were sent in reply on October 13. This confirms submission, not acceptance or merge. | The v7.2 revision handles the two reported mode/uclamp cases in the tests and reduces unchanged-default cost from about 616.51 to 162.39 ns/call. It is not a complete attribution of the earlier release slowdown. |

## Unsent candidates and closed diagnostic

Four unsent report or optimization-discussion candidates and one unsent but closed diagnostic bundle are
grouped under [`candidate/`](candidate/). Inclusion there does not create an
active experiment queue or automatically recommend an upstream report.

| Candidate | Current disposition | Core evidence |
| --- | --- | --- |
| [`lazyfree PTE-batch failure`](candidate/mm-lazyfree-dirty-pte-batch-354dffd2/) | Retained optimization-discussion candidate; no application benefit established; not sent. | Same-v7.2 prototype reduces PAGEOUT by 71.37% (60.69 us/32 MiB); lifecycle means change by -1.53%/-2.26%, comparable to drift and variation. |
| [`fair synchronous handoff`](candidate/sched-fair-delayed-requeue-handoff-333238da/) | Technical-discussion candidate; not submitted upstream. A diagnostic revert is not a fix. | Reverting `333238da9a19` reduces no/light-computation roundtrips by 11.31%/9.58%; no stable material advantage with a third busy thread. |
| [`async cancel miss`](candidate/io-uring-async-cancel-miss-5623eb1e/) | Reportable, but scoped to the guaranteed-miss slow path and the whole commit. | Exact A/B `+8.833%`, hit control `-0.562%`. |
| [`region API ring lifecycle`](candidate/io-uring-region-api-ring-lifecycle/) | Prepared; real-world impact is limited to repeated short-lived ring setup and teardown. | Release `+10.832%`; adjacent direct-parent pairs `+4.563%` and `+3.019%`. |
| [`NOP diagnostic control`](candidate/io-uring-nop-diagnostic-control/) | No upstream regression report proposed; retained as diagnostic-interface cost evidence. | Release plain/inject `+15.801%/+16.240%`; exact-commit component `+8.311%/+8.692%`. |

## Existing-patch validation

Status checked on 2026-10-10; the earlier validation measurements are unchanged.

- [RT sysctl read rebuild guard](sched-rt-sysctl-read-rebuild/):
  Joseph Salisbury's existing proposal, context-adapted to v7.2, reduces the
  tested RT read-loop cost by 96.9%. Successful writes retain maintenance;
  limited correctness checks passed. Feedback was sent September 24 and separately
  resent to Joseph's kernel.org address after the Oracle address bounced. No technical
  reply was found; the vacation response is not one. Current mainline still rebuilds
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

## Evidence policy

- Keep curated summaries, standalone reproducers, compact CSV/TSV/JSON data,
  and small attribution probes needed to understand a claim.
- Exclude private drafts, failed scratch logs, bulky raw runner workspaces,
  rebuildable build products, and local-only archives.
- Use immutable repository commit links in upstream email.
- State workload scope and caveats directly; do not turn a narrow
  source-calibrated result into a generic subsystem claim.
