# Upstream status

Latest check, 2026-10-10: validation was sent to the original thread on September
23 UTC (September 24 in UTC+08:00), then resent directly to `jsalisbury@kernel.org`
after the Oracle address bounced. The [public validation reply](https://lists.openwall.net/linux-kernel/2026/09/23/2523)
is archived. No technical response was found in Gmail, including Spam/Trash;
the vacation responder is not a maintainer review. Mainline `3857c2fe5449`
(v7.3-rc6) still calls `rebuild_sched_domains()` after reads and failed writes.
The proposal is not present in that inspected path. No new timing was performed.

The following is the earlier, pre-send September 23 snapshot.

Checked on 2026-09-23 UTC. This bundle validates Joseph Salisbury's existing
proposal; it is not a new patch submission or a claim that upstream accepted it.

- Author: Joseph Salisbury `<joseph.salisbury@oracle.com>`.
- Subject: `[PATCH] sched/rt: Rebuild domains only after successful RT sysctl writes`.
- Original Message-ID: `<20260313183716.990792-1-joseph.salisbury@oracle.com>`.
- [Public thread](https://patchew.org/linux/20260313183716.990792-1-joseph.salisbury@oracle.com/),
  [original mbox](https://patchew.org/linux/20260313183716.990792-1-joseph.salisbury@oracle.com/mbox).
- The inspected thread page contains the original proposal and no displayed
  replies. This is not proof that no separate discussion or revision exists.
- Inspected mainline: `fe2ec83746e501645709761605c2464a44fd2929`.
  Its [`sched_rt_handler()`](https://github.com/torvalds/linux/blob/fe2ec83746e501645709761605c2464a44fd2929/kernel/sched/rt.c)
  still calls `rebuild_sched_domains()` without the successful-write guard.
  The proposal is therefore not present in that checked mainline version.
  Maintainer trees and all stable branches were not exhaustively checked.
- Validation feedback is prepared for the original thread but has not yet been
  sent. No acceptance,
  `Tested-by` inclusion, or merge is claimed.

The tested patch changes only the context around the three original logic
changes to apply to v7.2. Joseph retains authorship of the proposal.
