# AppArmor AF_UNIX send slowdown after `6456ccbd2ff7`

This bundle records a focused AF_UNIX datagram send slowdown caused by the
exact source delta of
[`6456ccbd2ff7`](https://github.com/torvalds/linux/commit/6456ccbd2ff72814b3c1b2e2a3a2145a2ced858d)
(`apparmor: fix regression in fs based unix sockets when using old abi`).

The primary reproducer uses `socketpair(AF_UNIX, SOCK_DGRAM)`, sends batches of
32 128-byte messages with `sendmmsg()`, and drains and validates the peer
outside the timed region. In a fresh-boot parent/child/parent sandwich, the
exact source delta made this standalone workload `15.295%` slower:

| point | mean ns/message |
| --- | ---: |
| parent A | 343.382 |
| child | 395.831 |
| parent B | 343.259 |

The parent midpoint was 343.320 ns/message, parent drift was `-0.036%`, and
the drop-first result was `+15.289%`. All 45 measured rows passed payload,
length, count, and empty-queue checks. The three runs used matched actual
`preempt=none`, CPU 2 with governor and EPP set to `performance`, and Turbo
disabled. `/proc/self/attr/current` reported `unconfined` at every point.

An independent run with the larger original experiment source changed the
actual runtime mode to `preempt=full` on every point and reproduced the result:
direct `sendmmsg()` was `14.841%` slower, while io_uring `IORING_OP_SEND` was
`17.953%` slower. These percentages are separate corroboration and are not
combined with the standalone result. Because the ordinary system call also
reproduces the signal, this is not attributed to `io_uring/net.c`.

A separate v6.16.12 -> v6.17.13 release-endpoint `perf` comparison also moved
in the same direction. The children overhead reported for
`security_unix_may_send()` was `0.27%` in both old controls and `6.77%` at the
new point, whose stack continued through `apparmor_unix_may_send()`,
`aa_unix_peer_perm()`, and `unix_peer_perm()`. This supports the AppArmor send
permission path attribution, but it is not the exact synthetic source pair and
does not decompose the standalone `15.295%` delta among individual helpers.

The original upstream commit was part of an AppArmor series rather than a
direct mainline-parent pair. The experiment therefore used a controlled
synthetic pair on one baseline: the parent merges the AppArmor topic prefix
through `50d56a1a366a`; the child adds only the exact two-file source delta of
`6456ccbd2ff7`. The child tree matches the same baseline with the AppArmor
prefix advanced through `6456ccbd2ff7`.

The commit fixes a real old AppArmor policy ABI correctness problem. This
evidence does not recommend reverting it. The narrow question for upstream is
whether the same correctness can be retained without the measured cost on
this unconfined AF_UNIX datagram send path.

## Contents

- [`bare-metal/`](bare-metal/) contains source and run identity, result
  summaries, and all selected measured rows;
- [`reproducer/`](reproducer/) contains both the concise standalone source and
  the unmodified original formal workload used for the `u0`/`s0` table;
- [`upstream-status/`](upstream-status/) records the dated duplicate/fix audit
  and maintainer routing.
