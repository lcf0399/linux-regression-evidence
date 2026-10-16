# Bare-metal exact A/B

Test date: 2026-07-29.

The sequence was a fresh-boot
`6a8118a77eec parent A -> 079afb081c42 child -> 6a8118a77eec parent B`
sandwich. The two commits are an exact direct-parent pair. Every point used the
same normalized configuration, GCC 15.2.0 toolchain, Kbuild metadata,
equal-length release strings, CPU policy, and workload binary.

The machine was an Intel Core i7-12700KF system with 32 GiB RAM. Timing was
pinned to P-core CPU 2. The scaling governor and EPP were `performance`, Turbo
was disabled, and the actual runtime preemption mode was `full` at all three
points. The command line requested `preempt=none`; the hard gate records the
actual mode and only compares matched points.

The formal scalar workload had three warm-ups and 15 measured rounds per
boot. Each measured round ran 512 cycles of 32 wait/wake pairs, or 16,384
pairs. The timer covers SQE preparation, submission, and CQE collection. Ring
creation and memory setup are outside timing.

| implementation | parent A | child | parent B | child vs midpoint | drop first | parent drift | max CV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| formal | 180.079 | 196.647 | 179.856 | `+9.268%` | `+9.269%` | `-0.124%` | `0.169%` |
| standalone | 180.171 | 197.856 | 179.502 | `+10.020%` | `+9.996%` | `-0.371%` | `0.250%` |

All 90 scalar timing rows passed semantic and state checks. An untimed child
trace hit `io_futex_prep()`, `io_futex_wait()`, `io_futex_wake()`, and
`io_futex_complete()` with the expected request shape. The exact child source
adds `io_req_track_inflight()` in `io_futex_prep()`; that helper was not used
as a separately required trace symbol because it may be inlined.

The matched formal `WAITV -> WAKE` profile changed by `+1.385%`, below the 5%
signal gate, and is not part of the regression claim.

## Jens patch validation

The supplied two-patch series was tested separately on frozen Linus master
`48a5a7ab8d6a`; those percentages are not subtracted from the direct-parent
result above. In a five-point private-futex sandwich, patch 1 was `3.392%`
faster than the baseline midpoint, while the complete series differed from
the patch-1 midpoint by only `-0.025%`. In a matched scalar shared-futex
`patch 1 A -> full series -> patch 1 B` sandwich, the full series was
`1.578%` faster, with `0.319%` control drift. All 120 private/shared measured
rows passed and every measured boot actually used `preempt=full`.

Compact values are in [`jens-patch-validation.tsv`](jens-patch-validation.tsv)
and
[`jens-patch2-shared-validation.tsv`](jens-patch2-shared-validation.tsv).
Exact attachment hashes and applied commit/tree identities are in
[`jens-patch-identity.tsv`](jens-patch-identity.tsv).
