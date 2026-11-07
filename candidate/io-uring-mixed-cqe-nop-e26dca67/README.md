# Ordinary-CQE overhead after mixed-CQE support

Status, 2026-11-05: **bounded attribution complete; upstream report drafted but not sent.**
Whether to send remains undecided. The measured cost is not dismissed merely
because it is 5.564%, lacks a ready patch, or explains only part of the release gap.
No further NOP attribution run is queued; real-I/O impact remains unestablished.
Found while running the MMTests recipe for fio's native NOP benchmark, not through model-generated workloads.

With ordinary 16-byte CQEs and 20 submitters, commit
`e26dca67fde194340582cfbb0c0bf661825e9e46`
(`io_uring: add support for IORING_SETUP_CQE_MIXED`) reduced throughput by **5.564%**.

| Exact comparison | Million accepted NOP submissions/s | Within-boot CV |
| --- | ---: | ---: |
| Parent `89a885972140`, first boot | 531.605 | 0.145% |
| Child `e26dca67fde1` | 501.944 | 0.045% |
| Same parent, return boot | 531.432 | 0.066% |

Each boot had a separate 100-second warmup and nine clean 100-second calls.
Parent return drift was −0.0324%. Raw outputs and complete-call timing were independently recomputed.
Both kernels used GCC 15.2, the same configuration apart from their local-version suffix,
full preemption, all 20 logical CPUs of an i7-12700KF, microcode 0x3e and Turbo disabled.
The original benchmark placement was retained; submitters were not individually pinned.

The NOP object is identical across this boundary. The shared completion path now
decodes CQE width and performs more capacity/format work even for ordinary CQEs.
The matched assembly walk for an ordinary cached completion grew from 20 to 35
instructions and from 4 to 6 conditional branches. This supports the explanation
but does not assign the entire throughput loss to one expression.

The larger v6.12.95-to-v7.3 NOP slowdown has other contributors. Neither its
20–22% screening difference nor gains from different optimization prototypes
are attributed entirely to this commit. No real-I/O or application gain is claimed.
Further tests are deferred unless upstream supplies a concrete question or change.

- [Chinese review and evidence links](README.zh-CN.md)
- [Reproduction](reproduce.md)
- Upstream report remains unsent; drafts and full exploratory records stay local.

The current public [mainline header](https://raw.githubusercontent.com/torvalds/linux/master/io_uring/io_uring.h)
still contains the generic capacity comparison checked on 2026-11-05. This is a source check,
not a performance test of current mainline. The original
[feature discussion](https://lore.gnuweeb.org/io-uring/20250821141957.680570-1-axboe%40kernel.dk/)
explains its memory-efficiency motivation. Recent CQE32 boundary fixes are a separate correctness issue.
Maintainer lookups against both the exact parent and the current mainline
MAINTAINERS returned Jens Axboe, io-uring and LKML. Refresh thread status if sending later.
