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
