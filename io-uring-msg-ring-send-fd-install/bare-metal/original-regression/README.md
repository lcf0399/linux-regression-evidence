# Original SEND_FD regression and TCP follow up

The exact introducing commit is
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b),
`io_uring/rsrc: get rid of per-ring io_rsrc_node list`.
Every exact comparison below used its direct parent `e410ffca5886`, the child,
and a return to the parent. These are original-regression measurements, not
measurements of Uzair's patches or the shared-128 prototype.

## Original first fill result

The original workload repeatedly installs one source fixed file into a new
4,096-slot target table, at queue depth 64. Only SEND_FD and its completions
are timed; registration, readback and destruction are outside the interval.

| Point | ns per install | CV |
| --- | ---: | ---: |
| Parent A | 102.476 | 0.705% |
| Child | 114.441 | 0.289% |
| Parent B | 102.576 | 0.340% |

The child is **11.621% slower** than the parent midpoint; drop-first is
11.649%, with parent drift 0.097%. All 45 measured rounds passed semantic
checks. [Measured rounds](measured-rounds.tsv), [point statistics](exact-ab-points.tsv),
[build identity](build-identity.tsv) and [source identity](source-identity.tsv)
preserve the original evidence.

Independent non-timing checks observed 128 successful installs on both sides,
with nested `io_rsrc_node_alloc()` entries changing from 0 to 128. These
[counts](mechanism-summary.tsv) establish the new path, not that one allocator
function explains the entire time difference. The change resolves per-ring
serialization and reclamation stalls; no revert is recommended.

## Registration and first fill follow up

Measured 2026-09-29. Each total interval starts before sparse registration and
ends after the final install. It excludes ring creation, readback, destruction
and I/O. Values are microseconds per complete batch, not per file.

| Entries | Parent midpoint | Child | Increase | Stability |
| ---: | ---: | ---: | ---: | --- |
| 4 | 6.937 | 7.516 | 8.346% | pass |
| 64 | 15.274 | 16.291 | 6.662% | pass |
| 256 | 36.342 | 40.001 | 10.070% | pass |
| 4,096 | 444.108 | 507.590 | 14.294% | pass |

The four-entry result adds 0.579 us per batch. It used the small-count extension
of the lifecycle source; the other three used the original lifecycle source.
The combined cost did increase: it was not merely shifted from registration
to installation. Phase statistics, including unstable registration-only
results, remain in [results.tsv](results.tsv). These totals must not be
compared directly with the earlier first-fill-only absolute timings.

## TCP follow up

Measured 2026-09-29 with distinct TCP connections. Each connection performs
one or 16 exchanges of a 64-byte request and echo after SEND_FD installation.
This is a synthetic single-process, CPU-2 loopback test, not the liburing proxy
program and not a verified production workload.

| Connections | Exchanges per connection | Parent midpoint us | Child us | Total change | Stability |
| ---: | ---: | ---: | ---: | ---: | --- |
| 4, fresh rings | 1 | 430.474 | 436.089 | — | fail |
| 4, fresh rings | 16 | 616.646 | 621.180 | — | fail |
| 64, reused rings | 1 | 1,191.463 | 1,198.009 | +0.549% | pass |
| 64, reused rings | 16 | 3,957.333 | 3,939.361 | −0.454% | pass |
| 256, reused rings | 1 | 4,575.742 | 4,577.195 | +0.032% | pass |
| 256, reused rings | 16 | 16,211.001 | 16,317.520 | +0.657% | pass |

All four stable totals changed by less than 1%, including the drop-first
check. At 256 connections, installation still increased from 33.593 to
36.525 us (+8.728%) and from 33.719 to 36.697 us (+8.830%), respectively.
Thus a local installation slowdown did not become a substantial overall
slowdown in these cases. This does not prove that every application is
unaffected. Four-connection totals and 4/64-connection installation phases
were unstable and do not support an effect claim.

The whole TCP interval includes connection setup, SEND_FD, real I/O and close.
For reused rings, one-time ring setup and final teardown are separate. See
the [exact timing boundaries and controls](../README.md).

## Supporting evidence and files

- [Release summaries](result-summary.tsv) retain the independent 6.12/6.13
  and 6.12.95/7.1.3 supporting comparisons.
- [Slot gradient](slot-gradient.tsv) retains the older 64–4,096 scope check;
  its noisier small points are not substituted for formal results.
- [Short-source cross-check](standalone-cross-check.tsv) was directionally
  consistent but had 2.645% control drift, above the 2% gate.
- [timing.tsv](timing.tsv), [results.tsv](results.tsv) and
  [provenance.json](provenance.json) contain all 6,930 follow-up trial rows,
  48 phase metrics, source/build/runtime identities and non-timing counts.

The consolidated files distinguish the four-entry management run, the
64/256/4,096 management run and the TCP run. They are not pooled into one
experiment. Use [the read-only verifier](../../reproducer/verify_followups.py)
to recalculate the follow-ups.
