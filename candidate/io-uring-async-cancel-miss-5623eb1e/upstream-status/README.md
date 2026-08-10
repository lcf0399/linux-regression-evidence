# Upstream status audit

Audit date: 2026-07-28.

Jens Axboe posted the introducing change as
[`[PATCH v2] io_uring/tctx: add separate lock for list of tctx's in ctx`](https://lore.kernel.org/io-uring/24a9e751-7442-4036-9b9f-8c144918c201@kernel.dk/),
which was merged as `5623eb1ed035`. The review discussed why the async-cancel
path must restore `TASK_RUNNING` before taking sleeping locks; it did not
report or measure this performance effect.

The commit removes a lock dependency between `ctx->uring_lock` and
`tctx->io_uring_lock` by adding separate protection for `ctx->tctx_list`. The
measured slowdown is therefore reported as a narrow trade-off around the whole
correctness commit, not as a request to revert it or as a mutex-only diagnosis.

Source checks of Linus master at `62cc90241548`, Axboe's
`for-7.3/io_uring` at `c905736a`, and Axboe's `for-next` on the audit date found
the same `tctx_lock`-protected async-cancel slow-path walk. No later change was
found that removes or bypasses it for a guaranteed key miss. Those trees were
not benchmarked, so this is a source-topology statement only.

Searches of the public io-uring archive for the exact commit and for async
cancel miss/slowdown/performance reports found no report matching this
workload. The intended route is therefore a reply to the original v2 thread,
with the exact whole-commit scope stated explicitly. Message IDs and source
references are in [`refs.tsv`](refs.tsv).
