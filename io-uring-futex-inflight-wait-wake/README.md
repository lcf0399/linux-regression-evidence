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

## Upstream response and patch test

Jens Axboe agreed that inflight tracking was unnecessarily applied to the
synchronous WAKE side and sent two patches. Patch 1 moves tracking to the WAIT
prep path, while patch 2 retains it only for requests that contain a private
wait.

I tested the exact two attachments on a frozen 2026-07-23 Linus master base
(`48a5a7ab8d6a`) in this order, with a fresh boot for every measured point:

```text
baseline A -> patch 1 A -> full series -> patch 1 B -> baseline B
```

The baseline midpoint was `196.616 ns/pair`, the patch-1 midpoint was
`189.948 ns/pair`, and the full series was `189.900 ns/pair`. Patch 1 was
`3.392%` faster than the baseline midpoint; the full series was `3.416%`
faster. The full series differed from the patch-1 midpoint by only `-0.025%`,
as expected for this private-futex workload. All 75 measured rows passed and
all five kernels actually ran with `preempt=full`.

This patch test uses a later source baseline than the direct-parent result
above. The two percentages must not be subtracted or treated as having the
same denominator.

A matched shared-futex extension then compared patch 1 A, the full series,
and patch 1 B. The full series was `1.578%` faster than the patch-1 midpoint;
the drop-first result was also `1.578%`. Patch-1 control drift was `0.319%`
and the maximum CV was `0.200%`. All 45 rows passed. This directly exercises
the scalar shared-WAIT path where patch 2 removes inflight tracking; shared
WAITV was not tested.

The original report was sent from Gmail on 2026-07-30 with Message-ID
`<CANGjgdn=R_qyUdE=j9za+vkmqcxacbP-84OHXF4nZ4ho9qRyVg@mail.gmail.com>`.
The patch-validation reply was sent on 2026-07-31 with Message-ID
`<CANGjgdkhQZWntcnpa2zBshGn_E7yaKDnPcSbH-HBBfXGWAw1+g@mail.gmail.com>`.
Its `In-Reply-To` and `References` headers correctly continue Jens' patch
reply thread, and its actual recipients match the original report. As of
2026-08-03, Gmail contains no later response. Direct lore requests returned
403 and exact web searches found no result, so public archival remains
independently unconfirmed; this is not evidence that the message was not sent
or archived. The Gmail mailbox was checked again on 2026-08-07 and still had
no later maintainer reply.

## Layout

- [`bare-metal/`](bare-metal/) contains compact exact A/B results and build
  identities;
- [`reproducer/`](reproducer/) contains the exact formal source and a shorter
  commented scalar standalone;
- [`upstream-status/`](upstream-status/) records the correctness thread and
  the dated duplicate/fix audit.
- [`bare-metal/jens-patch-validation.tsv`](bare-metal/jens-patch-validation.tsv)
  records the compact five-point patch result.
- [`bare-metal/jens-patch2-shared-validation.tsv`](bare-metal/jens-patch2-shared-validation.tsv)
  records the matched shared-futex result for patch 2.
- [`bare-metal/jens-patch-identity.tsv`](bare-metal/jens-patch-identity.tsv)
  records the exact attachment hashes and applied commit/tree identities.
