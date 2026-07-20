# fsnotify concurrent inotify watch add/remove regression

Status: upstream report candidate; not yet sent.

This bundle documents a narrow, repeatable slowdown in concurrent inotify
watch setup and teardown introduced by Linux commit
[`94bd01253c3d`](https://github.com/torvalds/linux/commit/94bd01253c3d5b1cd8955bdadeed24af02088094)
(`fsnotify: Track inode connectors for a superblock`).

## Exact parent/child result

The primary bare-metal experiment used three independent boots in this order:

```text
6c790212c588 parent A
-> 94bd01253c3d child
-> 6c790212c588 parent B
```

The P8 distinct-inode aggregate worker-time metric was slower in the child by
`17.65%` and `15.11%` relative to the two parent controls. The two parent measurements differed by
only `2.18%`. A matched distinct/shared ratio worsened by `17.67%` and
`21.64%`, with `3.32%` parent drift.

The timed case creates 96 independent inotify instances and 96 distinct file
inodes outside the timed region. Eight pinned worker threads then add one watch
per inode and remove it. The matched shared-inode control keeps the same 96
inotify instances and pathnames, but all pathnames are hard links to one inode;
a keeper watch ensures that connector creation and destruction do not enter the
timed region. Each boot used two warm-up rounds followed by 25 measured rounds.

Existing-mask and path-lookup controls changed by less than `0.3%`. The P8
shared-inode case was slightly faster in the child, so the result is not a
machine-wide or generic inotify slowdown. Compact results are in
[`bare-metal/exact-ab-summary.tsv`](bare-metal/exact-ab-summary.tsv).

## Concurrency scaling extension

A second exact parent/child/parent sandwich tested P6 and P8 on distinct
physical P-cores, plus an auxiliary 16-worker point using both SMT threads of
all eight P-cores. The P6 distinct case was slower in the child by `9.03%` and
`9.56%`; its distinct/shared ratio worsened by `10.75%` and `10.85%`. The
same-window P8 point independently reproduced the signal at `19.58%` and
`19.11%` absolute, and `17.62%` and `16.80%` paired. Parent drift stayed below
`1.4%`, while shared-inode and negative-control cases had no matching
slowdown.

In the earlier exact run, P1 and P4 distinct changed by less than `2.1%`.
Together these runs place the material onset between P4 and P6 for this
machine and 96-watch workload; this is not a universal application or CPU
threshold. The 16-worker SMT-packed point had the same absolute direction,
but its child paired CV was `15.006954%`, just above the preregistered `15%`
gate, so it is auxiliary rather than primary evidence. See
[`bare-metal/scaling-extension-summary.tsv`](bare-metal/scaling-extension-summary.tsv).

## Focused mechanism evidence

Same-commit diagnostic builds separate two parts of the new per-superblock
connector-list maintenance:

- a `lock-only` build keeps the `list_lock` acquisition and release but removes
  list pointer updates;
- a `nolist` build removes both the list operations and their lock pair.

In a five-boot `full -> lock-only -> full -> nolist -> full` sequence, the P8
distinct case recovered `14.27%` in `lock-only` and `21.43%` in `nolist`.
The paired metric recovered `12.70%` and `20.46%`. This supports material cost
from both list mutation/additional lock hold time and bare lock handoff. These
builds are attribution probes, not safe fix candidates.

See
[`bare-metal/focused-mechanism-summary.tsv`](bare-metal/focused-mechanism-summary.tsv).

## Real-software topology gate

The exact timing benchmark is synthetic, so a separate trace checked whether
unmodified user software can create the same state shape. Eight stock
`inotifywait -m -r` processes from Ubuntu `inotify-tools 4.25.9.0-1` watched
eight non-overlapping subtrees of a real Linux 7.1.3 source tree on one ext4
superblock.

The processes created 4,177 directory watches. For every process, the observed
`fsnotify_add_mark_locked()` calls, `fsnotify_inode_mark_connector` allocations,
and `fsnotify_detach_connector_from_object()` calls exactly matched its watch
count. Add-mark activity appeared on nine CPUs, and one-millisecond buckets
contained activity from as many as all eight watcher processes.

This gate establishes topology and kernel-path reachability only. It does not
provide application-level timing. Starting eight watchers together was
intentional orchestration, although the tool, recursive traversal, source tree,
and inotify semantics were unmodified. See
[`real-topology/README.md`](real-topology/README.md).

## Latest upstream status audit

An audit on 2026-07-20 checked the latest released kernel (`v7.1.4`),
`v7.2-rc4`, Linus' master at `1590cf032971`, `next-20260717`, and Jan Kara's
linux-fs `fsnotify`, `for_next`, and `for_linus` branches. Every checked tree
still has the `list_lock` plus `list_add()`/`list_del()` operations introduced
by `94bd01253c3d` in the connector create and detach paths. Blame of those
lines still points to `94bd01253c3d`.

History searches for the exact operations and their key symbols found no
later optimization or equivalent fix. The only subsequent matching commit is
`a05fc7edd988`, which consumes the connector list during unmount rather than
removing its add/remove cost. Searches of the official linux-fsdevel and
regressions public-inbox histories through July 20 found neither a later patch
for this cost nor an existing regression report for `94bd`.

This is a source-history and prior-art audit, not a runtime claim that later
kernels have exactly the same percentage slowdown as the exact parent/child
pair. Exact refs and the audit boundary are recorded in
[`upstream-status/README.md`](upstream-status/README.md).

## Scope and tradeoff

This is not a claim that all fsnotify event delivery, all inotify users, or a
specific application became slower. The performance claim is limited to
concurrent distinct-inode watch add/remove around the exact commit.

The connector list introduced by `94bd01253c3d` is used by the following
[`a05fc7edd988`](https://github.com/torvalds/linux/commit/a05fc7edd988c176491487ef0ae4dbf5f7a64cd7)
change to destroy sparse inode marks efficiently during unmount and to support
the associated lifetime/race fix. This evidence therefore does not suggest a
revert. The upstream question is whether the common watch add/remove path can
be made cheaper while preserving that correctness and sparse-unmount benefit.

The original patch discussion focused on unmount efficiency and inode
lifetime correctness. During v2 review, lockless RCU traversal was mentioned,
but contention was not expected because inode-notification mark add/remove was
considered infrequent. The exact A/B here exercises that add/remove path under
concurrent watcher initialization. No earlier timing result for this path was
found in the reviewed series discussion.

References: [v3 cover](https://lore.kernel.org/linux-fsdevel/20260121135513.12008-1-jack@suse.cz/),
[v2 locking discussion](https://lore.kernel.org/linux-fsdevel/20260123-mengenlehre-wildhasen-46e47a6e7558@brauner/),
[maintainer response](https://lore.kernel.org/linux-fsdevel/m5a3dyhvpnjhyjmxae2o2sd2azhynbrupmhzsy2fbgomhdcyow@imnv6ytjaxfi/).

## Bundle layout

- [`bare-metal/`](bare-metal/README.md): exact A/B identity and compact
  timing, scaling, and mechanism summaries;
- [`reproducer/`](reproducer/README.md): standalone semantic benchmark and
  distinct/shared paired runner;
- [`real-topology/`](real-topology/README.md): compact stock-`inotifywait`
  trace summaries and rerun script;
- [`upstream-status/`](upstream-status/README.md): latest release, mainline,
  linux-next, maintainer-tree, and mailing-list prior-art audit.
