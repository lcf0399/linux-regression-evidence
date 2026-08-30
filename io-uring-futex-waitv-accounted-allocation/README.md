# io_uring WAITV accounted-allocation regression

This bundle records a narrow io_uring futex `WAITV -> WAKE` slowdown
introduced by
[`6e0d71c288fd`](https://github.com/torvalds/linux/commit/6e0d71c288fdcf5866f5d0c6cde850a091cc3c55)
(`io_uring/futex: use GFP_KERNEL_ACCOUNT for futex data allocation`). The
direct-parent diff changes the per-WAITV allocation from `GFP_KERNEL` to
`GFP_KERNEL_ACCOUNT` and nothing else.

The formal raw-UAPI workload uses one ring, eight independent wait vectors,
and eight cacheline-separated private futex words per vector. Each timed cycle
submits eight `IORING_OP_FUTEX_WAITV` requests, wakes element 3 in each vector,
and validates all 16 CQEs. An untimed wake then proves that completion removed
the seven remaining waiters from every vector.

In a fresh-boot direct-parent sandwich, the child was `8.091%` slower than the
parent midpoint. A separate 424-line standalone reproduced `6.488%`:

| implementation | parent A ns/pair | child | parent B | child vs midpoint |
| --- | ---: | ---: | ---: | ---: |
| formal source | 1045.687 | 1128.704 | 1042.755 | `+8.091%` |
| standalone | 1040.270 | 1110.916 | 1046.198 | `+6.488%` |

The formal drop-first result was `+8.080%`, parent drift was `-0.280%`, and
the maximum CV was `0.151%`. The standalone drop-first result was `+6.511%`
with `+0.570%` parent drift. All 90 WAITV timing rows passed the semantic and
state checks. All compared kernels actually ran with `preempt=full`.

A matched scalar `WAIT -> WAKE` control changed by `-0.555%`. The claim is
therefore limited to the WAITV preparation/allocation path above. It is not a
general futex or io_uring performance claim.

A current-baseline diagnostic on unmodified v7.2 used a direct child that only
changed the same allocation back to `GFP_KERNEL`. The no-account child was
`8.123%` faster than the midpoint of two v7.2 controls; the separate
standalone was `8.998%` faster, while the scalar control changed by only
`+0.089%`. Absolute control drift was at most `0.865%`, and every CV was below
`0.15%`. Later source changes therefore had not removed this narrow WAITV
allocation cost by v7.2. This diagnostic neither recommends removing memcg
accounting nor replaces the exact introducing-commit evidence above. Compact
results are in
[`bare-metal/current-v72-diagnostic-summary.tsv`](bare-metal/current-v72-diagnostic-summary.tsv).

The commit adds memory-cgroup accounting. This evidence does not recommend
reverting that resource-accounting behavior; it asks whether the same
accounting can be retained with lower per-request overhead.

The 2026-08-30 upstream refresh found that Linus master `08dbfad3f504` and the
io_uring maintainer's `for-next` `44e96e364b04` both retain this
`GFP_KERNEL_ACCOUNT` allocation. The original patch thread still has no reply,
and no equivalent performance optimization was found. The public evidence and
a narrow reply to the original patch thread are prepared, but the reply has
not yet been sent. The separately fixed futex inflight-WAKE regression is a
different cost and does not resolve this WAITV allocation difference.

## Layout

- [`bare-metal/`](bare-metal/) contains compact exact A/B data and identities;
- [`reproducer/`](reproducer/) contains the exact formal source and the shorter
  commented standalone;
- [`upstream-status/`](upstream-status/) records the original patch thread and
  the dated duplicate/fix audit.
