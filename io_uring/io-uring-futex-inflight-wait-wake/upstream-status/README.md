# Upstream status audit

Historical source/thread audit: 2026-08-03. Latest mailbox and stable-queue
status check: 2026-08-24.

Current disposition: resolved upstream. Commit
[`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef)
removes inflight tracking from synchronous FUTEX_WAKE, retains the required
WAIT lifetime handling, and carries
`Reported-by: Chengfeng Lin <lin2530632123@gmail.com>`.

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
The report was therefore sent as a reply to the original `[PATCH 2/2]`
thread. Exact references are in [`refs.tsv`](refs.tsv).

Jens Axboe replied that inflight tracking was a heavy mechanism for this
operation and was not needed on the synchronous WAKE side. He then sent two
patches in the report thread:

1. move inflight tracking from the shared scalar prep function to WAIT-only
   prep, so WAKE is not tracked;
2. retain tracking only for requests that contain a private scalar or vector
   wait, since shared waits do not depend on the per-`mm` private futex hash
   lifetime.

The exact attachments were tested on frozen master commit `48a5a7ab8d6a`.
Patch 1 improved the original private WAIT/WAKE workload by `3.392%`; the full
series improved it by `3.416%`. Full versus patch 1 changed by only `-0.025%`,
which is consistent with patch 2 targeting the shared-wait branch rather than
this private workload.

A matched scalar shared-futex run then compared patch 1 A, the full series,
and patch 1 B. The full series was `1.578%` faster than the patch-1 midpoint,
with `0.319%` control drift and a matching `1.578%` drop-first result. This
directly confirms a small improvement on patch 2's scalar shared-WAIT target
path. Shared WAITV remains untested.

The patch-validation reply was sent from Gmail on 2026-07-31 with Message-ID
`<CANGjgdkhQZWntcnpa2zBshGn_E7yaKDnPcSbH-HBBfXGWAw1+g@mail.gmail.com>`.
The original report has Message-ID
`<CANGjgdn=R_qyUdE=j9za+vkmqcxacbP-84OHXF4nZ4ho9qRyVg@mail.gmail.com>`.
Both are present in Gmail `SENT`; the validation reply has the expected
`In-Reply-To`, `References`, and recipients.

On 2026-08-24, Greg Kroah-Hartman sent three stable-queue notifications for
the upstream fix:

- 7.1: `<2026082416-chili-empower-39fb@gregkh>`;
- 6.18: `<2026082407-cruncher-carried-3088@gregkh>`;
- 7.2: `<2026082423-upstate-nature-93d0@gregkh>`.

These messages establish that the fix was selected for all three stable
queues. They do not establish that a released 6.18.x, 7.1.x, or 7.2.x kernel
already contains it. No further experiment is scheduled unless release
validation fails or upstream requests more evidence.
