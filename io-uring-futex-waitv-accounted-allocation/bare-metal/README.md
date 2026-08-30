# Bare-metal exact A/B

Test date: 2026-07-30.

The sequence was a fresh-boot
`816095894c0f parent A -> 6e0d71c288fd child -> 816095894c0f parent B`
sandwich. The commits are an exact direct-parent pair. Every point used the
same normalized configuration, GCC 15.2.0, Kbuild metadata, equal-length
release strings, CPU policy, and workload binaries.

The machine was an Intel Core i7-12700KF with 32 GiB RAM. Timing was pinned to
P-core CPU 2. Governor and EPP were `performance`, Turbo was disabled, and the
actual preemption mode was `full` at all three points. The command line
requested `preempt=none`; the hard gate records and compares the actual mode.

Each implementation used three warm-up rounds and 15 measured rounds per
boot. A measured round ran 512 cycles of eight WAITV/wake pairs, or 4,096
pairs. Setup and the residual-waiter check were outside timing.

| implementation | parent A | child | parent B | child vs midpoint | drop first | parent drift | max CV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| formal | 1045.687 | 1128.704 | 1042.755 | `+8.091%` | `+8.080%` | `-0.280%` | `0.151%` |
| standalone | 1040.270 | 1110.916 | 1046.198 | `+6.488%` | `+6.511%` | `+0.570%` | `0.142%` |

All 90 WAITV timing rows passed the semantic checks. The formal child trace
observed 128 `io_futexv_prep()`, 128 `io_futexv_wait()`, and 128
`io_futexv_complete()` calls, plus the expected wake path. The standalone
child smoke observed 16 of each WAITV function and 32 wake calls.

The matched formal scalar control changed by `-0.555%` and is not part of the
regression claim.
