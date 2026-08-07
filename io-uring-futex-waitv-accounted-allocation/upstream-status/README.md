# Upstream status audit

Audit date: 2026-07-30.

The introducing patch was posted by Jens Axboe as
`[PATCH for-next] io_uring/futex: use GFP_KERNEL_ACCOUNT for futex data allocation`
on 2026-01-25. The archived thread has no replies. The patch says the purpose
is to make the allocation participate in memory-cgroup accounting.

The patch was included in the io_uring core pull request for Linux 7.0. Current
Linus master at the audit point (`fc02acf6ac0c`) still uses
`GFP_KERNEL_ACCOUNT` in `io_futexv_prep()`. Later changes to this source region
were a mechanical allocation-helper conversion and an unrelated partial-wake
correctness fix; no equivalent performance optimization or matching report
was found.

The appropriate route is therefore a reply to the original patch thread, not
a new broad regression report. The reply should preserve the accounting goal
and ask whether its per-WAITV allocation overhead can be reduced.
