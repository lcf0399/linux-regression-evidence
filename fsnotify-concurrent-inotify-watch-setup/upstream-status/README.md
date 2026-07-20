# Upstream status and prior-art audit

Audit date: 2026-07-20.

## Outcome

No source-level fix or equivalent optimization for the connector add/remove
cost was found in the latest release, current mainline, linux-next, or the
fsnotify maintainer branches checked below. All of them still execute the
`list_lock` plus `list_add()`/`list_del()` operations introduced by
[`94bd01253c3d`](https://github.com/torvalds/linux/commit/94bd01253c3d5b1cd8955bdadeed24af02088094)
when an inode connector is created or detached.

This conclusion is deliberately narrower than a latest-kernel timing claim.
The exact slowdown percentages in this bundle come from the direct
`6c790212c588` parent versus `94bd01253c3d` child experiment. No timing run on
`v7.2-rc4` or Linus' 2026-07-19 tip was used to claim that a later kernel has
the identical percentage slowdown.

## Checked refs

The refs were fetched from the official kernel.org repositories. Exact object
IDs and dates are also recorded in [`refs.tsv`](refs.tsv).

| Layer | Ref | Exact commit | Date |
| --- | --- | --- | --- |
| latest release | `v7.1.4` | `7a5cef0db4795d9d453a12e0f61b5b7634fc4d40` | 2026-07-18 |
| mainline release candidate | `v7.2-rc4` | `1590cf0329716306e948a8fc29f1d3ee87d3989f` | 2026-07-19 |
| Linus tip | `master` | `1590cf0329716306e948a8fc29f1d3ee87d3989f` | 2026-07-19 |
| linux-next | `next-20260717` | `0718283ab28bc3907e10b61a6b4be6fefa1cbb2f` | 2026-07-18 |
| linux-fs | `fsnotify` | `a3aa899823dda059ab88a58254f9a605e03ec275` | 2026-07-03 |
| linux-fs | `for_next` | `9fdf954edc4783314931f150413dc8afae18754c` | 2026-07-17 |
| linux-fs | `for_linus` | `5163e6ee1ea744d412fe516235bfd9cab15141dc` | 2026-06-12 |

The release boundary was cross-checked against
[`kernel.org`](https://www.kernel.org/), which listed `7.1.4` as the latest
stable release and `7.2-rc4` as mainline on the audit date.

## Source-history check

For every ref, the audit inspected `fs/notify/mark.c` and searched history for
the exact operations and the symbols `inode_conn_list`, `conns_list`, and
`list_lock`. At Linus' checked tip the relevant code remains:

```c
spin_lock(&sbinfo->list_lock);
list_add(&iconn->conns_list, &sbinfo->inode_conn_list);
spin_unlock(&sbinfo->list_lock);

/* connector detach */
spin_lock(&sbinfo->list_lock);
list_del(&iconn->conns_list);
spin_unlock(&sbinfo->list_lock);
```

Blame for the create-path lines 808-810 and detach-path lines 825-827 still
points to `94bd01253c3d`. The exact checked source is available in
[Linus' tree](https://github.com/torvalds/linux/blob/1590cf0329716306e948a8fc29f1d3ee87d3989f/fs/notify/mark.c#L808-L827).

The only later key-symbol history match was
[`a05fc7edd988`](https://github.com/torvalds/linux/commit/a05fc7edd988c176491487ef0ae4dbf5f7a64cd7)
(`fsnotify: Use connector list for destroying inode marks`). That commit uses
the list during unmount; it does not remove or reduce connector list
maintenance on watch creation and teardown. It is therefore a dependent
consumer of `94bd`, not a performance fix for this report.

## Mailing-list and maintainer check

The official linux-fsdevel public-inbox history from 2025-03-21 through
2026-07-20 and the complete regressions archive from 2021-04-06 through
2026-07-20 were searched by subject, commit ID, and the exact connector-list
symbols. The audit found the original series and review, but no later fix
patch and no existing regression report for this add/remove cost.

In the original review, lockless RCU traversal was discussed, while the
maintainer response explained that inode-notification mark add/remove was
expected to be infrequent. See the
[v3 cover](https://lore.kernel.org/linux-fsdevel/20260121135513.12008-1-jack@suse.cz/),
[locking review](https://lore.kernel.org/linux-fsdevel/20260123-mengenlehre-wildhasen-46e47a6e7558@brauner/),
and [maintainer response](https://lore.kernel.org/linux-fsdevel/m5a3dyhvpnjhyjmxae2o2sd2azhynbrupmhzsy2fbgomhdcyow@imnv6ytjaxfi/).

Running the current `scripts/get_maintainer.pl` on the exact source diff
returned Jan Kara, Amir Goldstein, `linux-fsdevel@vger.kernel.org`, and
`linux-kernel@vger.kernel.org`. Christian Brauner remains a relevant CC
because he reviewed both `94bd` and `a05fc7`.

## Before-send decision

There is no candidate fix to validate in a new parent/fix A/B. The upstream
draft therefore reports the exact parent/child measurements, states the
latest-source audit separately, and asks whether optimizing this tradeoff is
worth pursuing. If a new revision or maintainer branch changes these hot-path
operations, it should be treated as a new candidate and retested rather than
assuming that source similarity proves a runtime result.
