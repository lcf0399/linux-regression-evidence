# Linux Regression Evidence

This repository contains curated public evidence for focused Linux performance
regressions, upstream follow-up, and patch validation.

## Sent or active upstream threads

Index updated on 2026-08-30. The WAITV source, thread, and recipient status was
refreshed on that date. The futex stable-queue status was checked live in Gmail
on 2026-08-24; the most recent consolidated check of the other older
Gmail-backed threads remains 2026-08-07. The mprotect entry is based on the
saved raw header from the student mailbox and the local evidence record.
Delivery, maintainer response, and the technical conclusion are kept separate.

The active I/O source-file exploration phase is closed. No new I/O target or
bare-metal experiment is queued. This repository now tracks only existing
upstream threads and user-selected decisions for the unsent material below.

| Evidence | Upstream state | Current technical state |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mprotect-shared-dirty-toggle/) | The latest mechanism-decomposition reply was sent on 2026-07-24 with intact `In-Reply-To` and `References`; no later response is recorded locally. | Exact A/B attributes a `39.77%` slowdown to `cac1db8c3aad`. Nested diagnostics recover `87.29%` of the gap from generic single-PTE update/flush handling and the normal page/folio lookup. Pedro v3 did not improve this workload; no safe fix is proposed. |
| [`tmpfs-flistxattr-small-list/`](tmpfs-flistxattr-small-list/) | Jan Kara pointed to `1e7cd8a53b72`; the requested exact bare-metal validation was sent to the thread. | The per-superblock cache commit removes the measured small-list tmpfs slowdown. Technically closed unless a different post-fix case appears. |
| [`fsnotify-concurrent-inotify-watch-setup/`](fsnotify-concurrent-inotify-watch-setup/) | Jan Kara accepted the P6/P8 contention interpretation and asked about one worker. The P1/P4 answer was sent on 2026-07-29; no later reply was present in Gmail on 2026-08-07. | P1/P4 are below the signal gate; P6/P8 show a parallel scalability loss on the superblock connector list. Upstream considers the current trade-off acceptable and mentioned unfinished rhashtable work that may improve it. |
| [`btrfs-remap-writeback-inhibition-v2/`](btrfs-remap-writeback-inhibition-v2/) | David Sterba acknowledged the validation, linked it from the patch record, and added the corrected patch to Btrfs `for-next`. | Independent patch validation found about `27%` lower clone cost and `22%` lower dedupe cost for the included 4 KiB micro-workload. This is patch validation, not a broad Btrfs claim. |
| [`apparmor-af-unix-send-old-abi-6456cc/`](apparmor-af-unix-send-old-abi-6456cc/) | Report sent on 2026-08-05. John Johansen replied that upstream will investigate and expects the regression can be improved, without promising full recovery. | The exact source delta makes unconfined AF_UNIX datagram `sendmmsg()` `15.295%` slower; an independent larger runner reproduces the direction. The report asks to retain the ABI correctness fix while reducing the added send-path cost. |
| [`io-uring-msg-ring-send-fd-install/`](io-uring-msg-ring-send-fd-install/) | Report sent on 2026-08-07 and publicly archived by the io-uring lore archive; no reply was present in Gmail at the 2026-08-07 audit. | Exact A/B attributes an `11.621%` fixed-file installation slowdown to `7029acd8a950`. A 64-to-4,096-slot check keeps the same direction. This is a narrow registration/update trade-off. |
| [`io-uring-futex-inflight-wait-wake/`](io-uring-futex-inflight-wait-wake/) | Resolved upstream: `73e701909747` carries the report attribution, and on 2026-08-24 Greg Kroah-Hartman queued it for 6.18, 7.1, and 7.2 stable. Queueing does not mean a stable release already contains it. | Direct-parent A/B remains `+9.268%`. Patch 1 improved the newer-master private workload by `3.392%`; patch 2 was private-neutral and improved the matched shared workload by `1.578%`. The unnecessary synchronous-WAKE tracking is fixed, so this line is closed unless release validation fails. |
| [`io-uring-futex-waitv-accounted-allocation/`](io-uring-futex-waitv-accounted-allocation/) | A reply to the original allocation patch thread is prepared but has not yet been sent. The 2026-08-30 refresh found no archived reply or equivalent optimization; current master/for-next retain the accounted allocation. | Exact direct-parent A/B is `+8.091%`, a separate standalone is `+6.488%`, and the scalar control is `-0.555%`. A separate v7.2 diagnostic still isolates about `8–9%` to the accounted WAITV allocation without proposing removal of memcg accounting. |

## Unsent candidates and closed diagnostic

Two unsent report candidates and one unsent but closed diagnostic bundle are
grouped under [`candidate/`](candidate/). Inclusion there does not create an
active experiment queue or automatically recommend an upstream report.

| Candidate | Current disposition | Core evidence |
| --- | --- | --- |
| [`async cancel miss`](candidate/io-uring-async-cancel-miss-5623eb1e/) | Reportable, but scoped to the guaranteed-miss slow path and the whole commit. | Exact A/B `+8.833%`, hit control `-0.562%`. |
| [`region API ring lifecycle`](candidate/io-uring-region-api-ring-lifecycle/) | Prepared; real-world impact is limited to repeated short-lived ring setup and teardown. | Release `+10.832%`; adjacent direct-parent pairs `+4.563%` and `+3.019%`. |
| [`NOP diagnostic control`](candidate/io-uring-nop-diagnostic-control/) | No upstream regression report proposed; retained as diagnostic-interface cost evidence. | Release plain/inject `+15.801%/+16.240%`; exact-commit component `+8.311%/+8.692%`. |

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
