# io_uring MSG_RING SEND_FD fixed-file installation

This bundle documents a narrow io_uring registration/update slowdown. The
workload uses `IORING_OP_MSG_RING` with `IORING_MSG_SEND_FD` to install one
source fixed file into empty slots of a second ring's fixed-file table.

The primary evidence is an exact direct-parent comparison around
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
(`io_uring/rsrc: get rid of per-ring io_rsrc_node list`):

| point | mean ns/install |
| --- | ---: |
| parent A | 102.476 |
| child | 114.441 |
| parent B | 102.576 |

The child was `11.621%` slower than the parent midpoint. Dropping the first
measured round gave `11.649%`, and parent drift was `0.097%`. All three boots
used the same workload binary, normalized kernel configuration and actual
runtime preemption mode (`full`). All 45 measured rows passed the semantic
checks.

An exact-kernel scope check at 64, 256, 1,024, and 4,096 target slots found
the same direction at every size (`+8.399%` to `+11.889%`). The smaller points
were noisier, so this does not replace the primary result; it shows that the
signal is not confined to the original 4,096-slot stress shape.

An untimed trace confirms the intended path. For 128 successful installs,
calls to `io_rsrc_node_alloc()` nested under `__io_fixed_fd_install()` changed
from 0 in the parent to 128 in the child. This establishes direct hit; it does
not assign the entire timing difference to one allocator function.

A later node-cache change, `ed9f3112a8a8`, is already present in Linux 7.1.3.
A matched Linux 6.12.95/7.1.3 run nevertheless showed the same direction
(`+15.602%`). The original change removes per-ring serialization and resource
reclamation stalls, so this bundle describes a measured registration-time
trade-off and does not recommend reverting it.

A 2026-08-22 cold/warm diagnostic explains why the cache did not close the
gap in the original workload. First fill of a new target ring used 128 fresh
node allocations and had zero inferred cache hits. After filling and
unregistering on the same ring, refill had 128 inferred hits and no fresh node
allocations. The v7.1.3
cold/warm midpoint changed from `137.789` to `109.643 ns/install`
(`-20.427%`), while the same priming shape changed v6.12.95 by only `-1.248%`.
The cache therefore improves reuse, not first fill of a new ring. It removed
most, but not all, of the release gap in this narrow diagnostic.

A later private patch proposed allocating `io_rsrc_node` from a dedicated
slab. On public v6.18-rc4, the unpatched, default-merged patch, and
`slab_nomerge` patch midpoints were respectively `119.394`, `129.586`, and
`129.721 ns/install` for the 4,096-slot first fill. The patched points were
`8.536%` and `8.649%` slower, while the 128-slot warm-reuse condition remained
within `0.4%` of unpatched. Default merging therefore did not explain the
result on this system.

In a separate single-variable diagnostic, both kernels used `slab_nomerge`
and differed only by `SLAB_ACCOUNT`. Removing that flag changed first fill by
`-9.382%`, 128-slot cold fill by `-8.833%`, and warm reuse by `+0.479%`.
This points to accounting work enabled by the flag as the source of the
patch's added cold-path cost. It is not a fix proposal: removing the flag
changes memory-cgroup accounting semantics. The private patch itself is not
redistributed in this bundle; the compact comparisons are recorded in
[`uzair-patch1-followup.tsv`](bare-metal/uzair-patch1-followup.tsv).

Uzair's revised v2 series removed the unintended accounting flag in patch 1
and added a 32-object bulk refill in patch 2. A six-boot current-base sequence
measured patch 1 at `-0.655%` versus unpatched and patch 1+2 at `+0.007%`
versus patch 1; both are effectively neutral at this boundary. An independent
untimed probe confirmed that patch 2 executed 2,313 full 32-object bulk calls,
including exactly 128 calls in every complete 4,096-slot cycle. Thus the
neutral result is not explained by a missed branch or partial bulk returns.
The formal timing, noisy 128-slot diagnostic, and mechanism counts are kept
separately in [`bare-metal/`](bare-metal/).

This is a focused synthetic microbenchmark. It is not an application
benchmark and makes no claim about the ordinary io_uring read/write fast path.

A shorter, commented F0-only source is also provided for code review. Its
independent exact-kernel cross-check measured `+11.145%`, close to the
`+11.621%` formal result. Because its two parent controls drifted by `2.645%`,
above the formal `2%` gate, that cross-check is directional only; the formal
claim remains bound to the unchanged 1,165-line source and table above.

## Layout

- [`bare-metal/`](bare-metal/): exact A/B results, slot-count scope check,
  node-cache cold/warm and private-patch diagnostics, compact identities and
  trace summaries;
- [`reproducer/`](reproducer/): exact experiment source, shorter `SEND_FD`
  standalone, cold/warm diagnostic, trace helper, validator and one-point runner;
- [`upstream-status/`](upstream-status/): introducing thread, later cache
  change and dated source audit;
- `email/`: local mail material and sending notes; intentionally ignored by
  Git and not part of the public evidence bundle.
