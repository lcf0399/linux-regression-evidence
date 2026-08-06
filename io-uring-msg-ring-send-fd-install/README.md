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

This is a focused synthetic microbenchmark. It is not an application
benchmark and makes no claim about the ordinary io_uring read/write fast path.

A shorter, commented F0-only source is also provided for code review. Its
independent exact-kernel cross-check measured `+11.145%`, close to the
`+11.621%` formal result. Because its two parent controls drifted by `2.645%`,
above the formal `2%` gate, that cross-check is directional only; the formal
claim remains bound to the unchanged 1,165-line source and table above.

## Layout

- [`bare-metal/`](bare-metal/): exact A/B results, slot-count scope check,
  compact identities and the direct-hit trace summary;
- [`reproducer/`](reproducer/): exact experiment source, shorter F0-only
  standalone, validator and one-point runner;
- [`upstream-status/`](upstream-status/): introducing thread, later cache
  change and dated source audit;
- `email/`: local upstream reply drafts and sending notes; intentionally
  ignored by Git and not part of the public evidence bundle.
