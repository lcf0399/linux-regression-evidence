# Upstream status audit

## Tracking checked 2026-10-19

Regzbot stopped tracking the report as inconclusive on October 13, citing lack
of feedback and a likely microbenchmark issue. This is a tracking disposition,
not a fix or a finding that required accounting can be removed. See the
[report entry and status distinctions](../../../upstream-status/README.md).

## Historical July 30 and August 30 source/thread audits

The source and search conclusions below are dated August 30. This tracking
update does not claim a fresh audit of all current branches.

The introducing patch was posted by Jens Axboe as
`[PATCH for-next] io_uring/futex: use GFP_KERNEL_ACCOUNT for futex data allocation`
on 2026-01-25. The archived thread has no replies. The patch says the purpose
is to make the allocation participate in memory-cgroup accounting.

The patch was included in the io_uring core pull request for Linux 7.0. Linus
master `08dbfad3f504` and the io_uring maintainer's current `for-next`
`44e96e364b04`, both checked on 2026-08-30, still use
`GFP_KERNEL_ACCOUNT` for the variable-sized allocation in
`io_futexv_prep()`. The source now uses `kzalloc_flex()`, and a separate fix
removed inflight tracking from synchronous WAKE, but neither change removes
the memcg-accounted WAITV allocation. Searches by the original thread, exact
commit, WAITV, accounting, and performance still found no equivalent
optimization or matching performance report; the archived patch thread still
has no replies.

The technical question is whether per-WAITV allocation overhead can be reduced
while preserving the accounting goal. The separately resolved inflight-WAKE
regression does not resolve this allocation difference.
