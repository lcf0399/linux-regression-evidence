# Upstream status audit

Audit date: 2026-07-29.

Robert Morris reported a use-after-free when a task was killed with a private
`IORING_OP_FUTEX_WAIT` still pending. Jens Axboe fixed it by marking scalar and
vector futex waits as inflight, allowing exit cancellation to find the
requests before their `mm` and private futex hash state disappear. The patch
was merged as `079afb081c42`.

That correctness and lifetime requirement is binding. The performance report
must not recommend a revert; it should ask whether an equivalent lower-cost
tracking design is possible.

The source audit checked Linus master at
`fc02acf6ac0ccde0c805c2daa9148683cdd01ba8` and a refreshed io_uring
`for-next` head at `e5f87b066744` on the audit date. Both still route scalar
and vector futex preparation through `io_req_track_inflight()`. The master
source was checked directly; the branches were not benchmarked.

Searches of the public io-uring archive for the exact commit and for futex
wait/inflight performance reports found the correctness report and patch
thread, but no matching performance report or later equivalent optimization.
The intended mail route is therefore a reply to the original `[PATCH 2/2]`
thread. Exact references are in [`refs.tsv`](refs.tsv).
