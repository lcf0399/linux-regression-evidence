# io_uring SEND_FD installation and cache tests

Updated 2026-10-01. **The original installation slowdown remains measurable,
but the stable loopback TCP cases did not show a substantial overall slowdown.
Neither fixed-limit bulk-refill implementation showed a clear benefit in the
tested cases.** These results do not support enlarging the default idle-node
cache or adopting the diagnostic prototype for performance.

This is a collection of synthetic management and TCP tests, not a claim about
all io_uring applications. The original commit fixes resource-management
problems; these measurements do not justify reverting it.

## Results by question

| Question | Comparison | Result and evidence |
| --- | --- | --- |
| Did the original change add installation cost? | Exact parent of `7029acd8a950` versus that commit, with a parent return | Original 4,096-install first fill: **+11.621%**. Follow-up registration plus first fill also increased at 4, 64, 256 and 4,096 entries. [Original regression](bare-metal/original-regression/README.md) |
| Does it slow the tested network task? | Same original parent/child pair | Stable 64/256-connection TCP totals changed by **less than 1%**, although 256-connection installation was about **8.7–8.8% slower**. Both four-connection TCP cases were unstable. [TCP results](bare-metal/original-regression/README.md#tcp-follow-up) |
| Do the old dedicated-slab and batch-32 patches help? | Unpatched v6.18-rc4 versus patches 1+2, with an unpatched return | No clear benefit. Registration plus first fill at 64 entries was **5.18% slower**; five stable TCP totals changed by less than 1%. [Dedicated slab and batch 32](bare-metal/dedicated-slab32/README.md) |
| Does shared kmalloc with batch 128 help? | Same v6.18-rc4 baseline versus an independent prototype, with an unpatched return | No clear benefit. Registration plus first fill: **+9.61% at 64**, **+4.42% at 256**. Five stable TCP totals: **+0.005% to +1.320%**. [Design, patch and results](bare-metal/shared-slab128/README.md) |
| Why not enlarge the cache? | Historical v3 on v6.18-rc4 | Replacing 4,096 entries improved, but replacing 64 did not; registering 4,096 and using 64 left **4,032 idle nodes**. [Retained v3 evidence](bare-metal/uzair-v3-prefill/README.md) |

The two fixed-limit patch tests are separate matched comparisons. Their
percentages must not be subtracted to estimate a batch-32 versus batch-128
effect. An unstable case is unresolved, not evidence of no effect. No matching
production application has been verified that frequently recycles more than
128 nodes as one batch on a single ring.

## Where to look

- [Methods and data map](bare-metal/README.md): timing boundaries, controls and
  standalone result verification.
- [Reproducers](reproducer/README.md): raw-UAPI C programs and build instructions;
  the latest management and TCP tests share one copy of their helpers.
- [Historical diagnostics](bare-metal/history/README.md): cold/warm caching,
  allocator accounting, and first-fill isolation. Useful distinct results are
  retained; repeated narratives are no longer mixed into the current results.
- [Dated upstream records](upstream-status/README.md): historical correspondence
  and source audit, not a claim that the current prototype has been accepted.

The 2026-10-01 update publishes all numeric rows and unstable metrics from the
new follow-ups. It does not publish private correspondence, host-control scripts,
credentials, binaries or full host logs. The reply prepared for Uzair remains
separate from this public evidence and has not been sent as part of this update.

[中文说明](README.zh-CN.md)
