# Measured hierarchical-counter derivative and bounded checks

This is an evaluation patch, not a claim of a finished general-purpose fix.
The [patch](hierarchical-with-rstat.patch) is byte-identical to the tested change
against MM commit `2967f3ccdb5fae956f1b085370ec292bab5c7e25`.

## Source and adaptation

Joanne Koong's [experimental commit](https://github.com/joannekoong/linux/commit/9092e0057f038ffca43ae3f35faf58c33b1d7c9b)
compares four accounting approaches; the hierarchical per-CPU idea was discussed
by Yosry Ahmed in the [public thread](https://lore-kernel.gnuweeb.org/cgroups/CAO9r8zNxnVYpwrsuB1Ofcwf-Lks2e%2BDL1Ys9CXZ5jozBug_ybQ@mail.gmail.com/T/).
This derivative selects that approach without the four-mode switch. It keeps
the existing `MEMCG_ZSWAP_B` updates because public usage and reclaim/shrinker
consumers still use them. The original experiment explicitly left those statistics
unfinished; it is not being treated as a submitted production-ready replacement.

The bodies of `memcg_zswap_hier_add()` and `memcg_zswap_hier_sum()` were
checked against the saved original commit patch and are byte-identical.
The complete patch is an adaptation, not an unchanged upstream patch: it
selects only this mode, retains the rstat updates and fits the tested MM base.

The derivative adds signed per-CPU compressed-byte counters, updates ancestors
on charge/uncharge under the existing RCU read-side section, and sums possible-CPU
counters for the limit pre-check. The final sum is clamped at zero. It leaves the
IRQ-latency protection and original statistics machinery intact.

Both upward updates and per-CPU summation have costs. Reading the sum is not an
atomic reservation or snapshot. This preserves a non-atomic pre-check design;
it does not promise no transient over-limit state under every interleaving.

## Actual kernel checks

Each row below passed once on each kernel, for 16 valid boundary checks total.
Normal 32 MiB workload admission checks were separate. No probe timings from
these checks enter the performance tables.

| Check | Bounded observation on both kernels |
| --- | --- |
| Leaf limit zero, then unlimited | Empty pool stays empty under zero; 2048 pages stored after raising the limit. |
| Ancestor limit zero, then unlimited | Same behavior with the ancestor limiting the child. |
| Leaf near limit, free and reuse | Partial retention (199 pages in this run); storage works after release/reuse. |
| Ancestor near limit | Partial retention under the ancestor limit; content and final release verified. |
| Dynamic lowering and raising | Pre-check, reclaim fallback and store returns observed separately, as below. |
| Cross-CPU release/reuse | CPU 0 stores, CPU 2 verifies/frees and stores again; reverse release succeeds. |
| Concurrent add/free | Two CPUs perform four handoffs; 2048 stored pages at each quiescent checkpoint, zero finally. |
| Offline with live objects | Actual `mem_cgroup_css_offline()` return observed while 2048 entries remain; subsequent uncharge belongs to the parent; 614472 charged/freed bytes match. |

The 199-page observation is not a universal quota guarantee. Overlapping commands
do not prove every internal interleaving; `rmdir()` alone was not used as proof of
completed object reparenting. All checks include content validation and cleanup.

## Retained failure: an incorrect dynamic-limit test premise

An initial baseline check assumed that lowering the limit to zero would reject
new stores without reclaiming existing entries. It failed: stored pages fell
from 2048 to one. This is retained as a failed test premise, not a kernel pass
or a prototype regression. The four earlier successful baseline cases were not
rerun; only the remaining cases used the corrected observation contract.

Source and direct-return checks show that `obj_cgroup_may_zswap()` rejection can
be followed by successful `shrink_memcg()` and then a store without a second
pre-check. In the corrected case, both kernels produced:

- Initial unlimited phase: 2048 permitted pre-checks and successful stores.
- After lowering to zero: 2048 rejected pre-checks; 1024 successful reclaims/stores
  and 1024 `-EAGAIN` reclaims/failed stores, leaving one stored entry.
- After raising the limit: 2048 permitted pre-checks and successful stores again.
  Contents of both sets and final release were verified.

The derivative matched this existing fallback behavior; these tests do not endorse
the policy as a strict instantaneous limit. [The compact check ledger](../bare-metal/counter-mm-pair/boundaries.tsv)
keeps the failed attempt separate from the 16 successful checks.

## What remains open

RCU races, CPU hotplug, 32-bit overflow/scaling, deeper hierarchies and production
scale have not been exhausted. Cache-hit restoration does not measure cold
decompression independently. Historical total-32-MiB eight-worker unlimited timing remains noisy;
the [separate 256 MiB supplement](../bare-metal/counter-eight-scale256/README.md)
has much smaller variation and no stable unlimited penalty, without establishing
equivalence or covering other scheduling/hierarchy conditions.
The demonstrated finite-limit gain and small single-worker control do not remove
these limitations or establish a best accounting design.
