# io_uring scalar futex wait/wake inflight-tracking regression

This bundle records a narrow scalar io_uring futex wait/wake slowdown
introduced by
[`079afb081c42`](https://github.com/torvalds/linux/commit/079afb081c4288e94d5e4223d3eb6306d853c68b)
(`io_uring/futex: mark wait requests as inflight`).

The formal raw-UAPI workload uses one ring and 32 cacheline-separated private
futex words. Each timed cycle submits 32 scalar `IORING_OP_FUTEX_WAIT`
requests, then 32 scalar `IORING_OP_FUTEX_WAKE` requests, and drains exactly
64 CQEs. Every wait result must be 0 and every wake result must be 1.

In a fresh-boot direct-parent sandwich, the child was `9.268%` slower than the
parent midpoint:

| implementation | parent A ns/pair | child | parent B | child vs midpoint |
| --- | ---: | ---: | ---: | ---: |
| formal source | 180.079 | 196.647 | 179.856 | `+9.268%` |
| 338-line standalone | 180.171 | 197.856 | 179.502 | `+10.020%` |

The formal drop-first result was `+9.269%`, parent drift was `-0.124%`, and
the maximum CV was `0.169%`. The standalone drop-first result was `+9.996%`
with `-0.371%` parent drift. All 90 scalar timing rows passed the CQE,
returned-value, timeout, overflow, outstanding-request, and CPU-affinity
checks. All compared kernels actually ran with `preempt=full`.

A matched `WAITV -> WAKE` profile changed by only `+1.385%`, below the
preregistered 5% signal gate. The claim is therefore limited to the scalar
wait/wake shape above.

The introducing commit fixes a use-after-free risk by keeping pending private
futex waits visible to exit cancellation before their `mm` state disappears.
Scalar WAIT and WAKE both use `io_futex_prep()`, so in the child both sides of
each measured pair execute the added tracking call. This evidence does not recommend
reverting the correctness fix; it asks whether the same lifetime guarantee can
be retained at lower per-request cost.

This is a focused synthetic microbenchmark, not an application benchmark or
a claim about futex, io_uring, or wait-vector performance in general.

## Layout

- [`bare-metal/`](bare-metal/) contains compact exact A/B results and build
  identities;
- [`reproducer/`](reproducer/) contains the exact formal source and a shorter
  commented scalar standalone;
- [`upstream-status/`](upstream-status/) records the correctness thread and
  the dated duplicate/fix audit.
