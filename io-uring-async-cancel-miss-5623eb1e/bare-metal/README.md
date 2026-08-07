# Bare-metal results

The exact comparison is around
[`5623eb1ed035`](https://github.com/torvalds/linux/commit/5623eb1ed035f01dfa620366a82b667545b10c82)
and its only direct parent `fc5ff2500976`. Three independent boots were run in
this order:

```text
fc5ff2500976 parent A -> 5623eb1ed035 child -> fc5ff2500976 parent B
```

Each point used 3 untimed warm-up rounds followed by 15 measured rounds. An
`A0_MISS` round contains 64 batches of 32 guaranteed-missing cancel attempts,
or 2,048 attempts. Before every batch, 32 eventfd poll requests are proven
pending. The timed region then submits the 32 missing-key cancel requests and
collects their `-ENOENT` CQEs. Cleanup of the real poll requests follows after
timing. `A0_HIT` uses the same basic shape but cancels the real poll keys.
The two profiles are compared across kernels separately; their absolute
latencies are not subtracted from each other.

| profile | parent A | child | parent B | child vs midpoint | drop first | parent drift | max CV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `A0_MISS` | 143.660 | 156.137 | 143.271 | `+8.833%` | `+8.807%` | `-0.271%` | `0.422%` |
| `A0_HIT` control | 131.641 | 130.324 | 130.480 | `-0.562%` | `-0.533%` | `-0.882%` | `0.490%` |

Units are ns/attempt. All 90 measured rows passed the expected-result, CQE,
pending-state, overflow, timeout, outstanding-request, and CPU-affinity checks.
The selected rows needed to recompute the statistics are in
[`measured-rounds.tsv`](measured-rounds.tsv); the compact statistics are in
[`result-summary.tsv`](result-summary.tsv).

The three kernels used the same normalized config, GCC 15.2.0 toolchain,
Kbuild metadata, equal-length release strings, and identical workload binary.
Requested/build/actual preemption was `none/dynamic/full` at every point, so
the actual runtime mode was matched at `full`. The process was pinned to
P-core logical CPU 2 on an Intel Core i7-12700KF system with 32 GiB RAM; the
governor and EPP were `performance`, and Turbo was disabled. Compact identities
are in [`build-identity.tsv`](build-identity.tsv).

## Concise standalone cross-check

The separately built 373-line standalone reproducer was also run in a fresh-
boot parent A -> child -> parent B sequence. Its means were 141.661, 154.102,
and 140.838 ns/attempt, respectively: `+9.099%` for the child versus the parent
midpoint. The drop-first result was `+9.108%`, parent drift was `-0.581%`, and
the maximum CV was `0.374%`. All 45 measured rows passed the semantic checks,
and actual preemption was matched at `full`.

This independently corroborates the exact-source result (`+8.833%`) but does
not replace it. The compact cross-check is in
[`standalone-cross-check.tsv`](standalone-cross-check.tsv).

## Untimed direct-hit check

On the child, the `A0_MISS` trace recorded 256 calls each to
`io_async_cancel()`, `io_try_cancel()`, and `io_poll_cancel()`; `A0_HIT`
recorded 128 each. These counts prove that both profiles reach the required
cancel path. The optional `io_cancel_req_match()` probe recorded zero and was
not a required gate. See [`direct-hit.tsv`](direct-hit.tsv).

## Attribution boundary

The exact commit adds `ctx->tctx_lock`, changes tctx-list synchronization and
walks, and restores `TASK_RUNNING` in the async-cancel slow path. This result
therefore attributes the narrow miss-path slowdown to the whole commit only.
It does not isolate the cost of one lock instruction or justify reverting the
correctness change.
