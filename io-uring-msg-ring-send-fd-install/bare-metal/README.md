# Bare-metal results

## Evidence map

The files are separated by evidence type rather than by date. No two TSV
files have the same evidentiary role or can be safely deleted as duplicates:

| group | files | purpose |
| --- | --- | --- |
| Primary exact result | [`source-identity.tsv`](source-identity.tsv), [`build-identity.tsv`](build-identity.tsv), [`exact-ab-points.tsv`](exact-ab-points.tsv), [`measured-rounds.tsv`](measured-rounds.tsv), [`result-summary.tsv`](result-summary.tsv) | source/build provenance, point statistics, selected raw rounds, and cross-window summary |
| Scope and direct hit | [`slot-gradient.tsv`](slot-gradient.tsv), [`standalone-cross-check.tsv`](standalone-cross-check.tsv), [`mechanism-summary.tsv`](mechanism-summary.tsv) | slot-count scope, readable-source cross-check, and exact path counts |
| Cache and allocation diagnosis | [`node-cache-cold-warm.tsv`](node-cache-cold-warm.tsv), [`node-cache-trace.tsv`](node-cache-trace.tsv), [`first-fill-allocation-diagnostics.tsv`](first-fill-allocation-diagnostics.tsv) | reuse timing, inferred hit counts, new-slab/perf observations, and both prefill diagnostics |
| Collaborator patches | [`uzair-patch1-followup.tsv`](uzair-patch1-followup.tsv), [`uzair-v2-bulk-refill.tsv`](uzair-v2-bulk-refill.tsv), [`uzair-v2-bulk-refill-mechanism.tsv`](uzair-v2-bulk-refill-mechanism.tsv) | dedicated-slab/accounting diagnostics and revised bulk-refill timing/path checks |
| v3 prefill and lifecycle follow-up | [`uzair-v3-prefill/`](uzair-v3-prefill/README.md) | unchanged v3 first fill, registration cost, repeated batch reuse, low utilization, memory/cleanup, and instability; 28 metrics with source/build identity |

The summary and selected-round files intentionally overlap on a few aggregate
numbers: the former is the compact index, while the latter is the minimal data
needed to recompute mean, CV, and drop-first. Timing and trace records remain
separate because they have different measurement scopes.

The primary result is an exact direct-parent comparison around
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
(`io_uring/rsrc: get rid of per-ring io_rsrc_node list`). Three independent
boots were run in this order:

```text
e410ffca5886 parent A -> 7029acd8a950 child -> e410ffca5886 parent B
```

Each point used 3 warm-up rounds followed by 15 measured rounds. Every round
performed 4,096 successful fixed-file installations in batches of 64. The
mean end-to-end cost in ns/install was:

| point | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 102.476 | 0.705% |
| child | 114.441 | 0.289% |
| parent B | 102.576 | 0.340% |

The child was `11.621%` slower than the midpoint of the two parent controls.
Dropping the first measured round from every point gave `11.649%`; parent
drift was `0.097%`. All 45 measured rows passed the CQE, slot-allocation,
sentinel, affinity, and unexpected-result checks.

The kernels used the same normalized config, GCC 15.2.0 toolchain, Kbuild
metadata, module-signing key, equal-length release strings, and identical
workload binary. Runtime preemption was `full` at all three points.
[`build-identity.tsv`](build-identity.tsv) and
[`exact-ab-points.tsv`](exact-ab-points.tsv) record the compact identities and
point statistics. [`measured-rounds.tsv`](measured-rounds.tsv) contains the 45
selected per-round timing rows needed to reproduce the mean, CV, drop-first
and semantic-pass checks without carrying the raw runner workspace.

## Supporting comparisons

Two independent matched sandwiches give the same direction:

- Linux 6.13 was `10.747%` slower than the Linux 6.12 control midpoint;
- Linux 7.1.3 was `15.602%` slower than the Linux 6.12.95 control midpoint.

These are release-level checks, not substitutes for the exact commit result.
All compared points had actual runtime `preempt=full`. The three comparisons
are collected in [`result-summary.tsv`](result-summary.tsv).

## Short-source cross-check

The 422-line F0-only source was also run across fresh exact-kernel boots in
the order parent A, child, parent B:

| point | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 101.239 | 0.440% |
| child | 114.010 | 1.776% |
| parent B | 103.917 | 0.441% |

The child was `11.145%` slower than the parent midpoint; drop-first gave
`11.147%`. All 45 rows passed the semantic checks and actual preemption was
`full` at every point. A separate untimed 256-operation smoke trace observed
256 calls each to `io_msg_ring()`, `io_msg_install_complete()`, and
`__io_fixed_fd_install()`.

Parent drift was `2.645%`, slightly above the formal `2%` gate. This result is
kept as a compact directional cross-check and is not promoted as a second
formal result. See [`standalone-cross-check.tsv`](standalone-cross-check.tsv).

## Slot-count scope check

The exact-kernel parent A, child, parent B sequence was repeated with 64, 256,
1,024, and 4,096 target slots, while keeping the queue depth at 64. The child
was respectively `11.853%`, `8.717%`, `8.399%`, and `11.889%` slower than the
matched parent midpoint. All 180 measured rows passed the semantic checks.

The 64-slot child and 256-slot parent controls were noisier than the larger
points, so this is a scope check rather than a replacement for the primary
4,096-slot result. It shows that the direction is already present at one
64-operation batch and is not confined to a very large fixed-file table. The
effect does not grow monotonically with the table size. See
[`slot-gradient.tsv`](slot-gradient.tsv).

## Untimed mechanism trace

An untimed parent/child trace used 128 successful SEND_FD installations on
each kernel. Both points observed 128 calls to `io_msg_ring()`,
`io_msg_install_complete()`, and `__io_fixed_fd_install()`. Calls to
`io_rsrc_node_alloc()` nested under fixed-file installation changed from 0 in
the parent to 128 in the child; total calls changed from 4 to 129.

This confirms that the workload directly reaches the new per-install resource
node path. It does not prove that one allocator function accounts for the full
timing difference. See [`mechanism-summary.tsv`](mechanism-summary.tsv).

## Node-cache cold/warm diagnostic

A follow-up on the same machine compared first fill of a new target ring
(cold) with fill, unregister, and refill on the same ring (warm). The
diagnostic uses 128 slots because `IO_ALLOC_CACHE_MAX` is 128 in v7.1.3. Only
the final 128 installations were timed. Each row aggregated 128 independent
subpairs, or 16,384 timed installations per condition. Fresh boots ran in the
order v7.1.3 A, v6.12.95, v7.1.3 B.

| point | cold ns/install | warm ns/install | warm vs cold |
| --- | ---: | ---: | ---: |
| v7.1.3 A | 139.531 | 109.143 | `-21.779%` |
| v6.12.95 | 106.357 | 105.030 | `-1.248%` |
| v7.1.3 B | 136.046 | 110.143 | `-19.040%` |

The v7.1.3 midpoint changed from `137.789` to `109.643 ns/install`
(`-20.427%`). A separate untimed trace observed 128 fresh node allocations
and zero inferred hits for cold, versus zero fresh node allocations and 128
inferred hits for warm. The surrounding v7.1.3 controls drifted by `-2.498%` for cold and
`+0.916%` for warm, so this is a mechanism diagnostic rather than a replacement
for the exact-commit result whose parent drift was only `0.097%`. Each v7.1.3
point independently showed a 19% to 22% paired effect.

The cache therefore improves reuse, but the original workload creates a new
ring every round and has no nodes to reuse on first fill. The v7.1.3 warm
midpoint remained `3.089%` slower than the v6.12.95 cold point; this diagnostic
does not split the remaining cost. See
[`node-cache-cold-warm.tsv`](node-cache-cold-warm.tsv) and
[`node-cache-trace.tsv`](node-cache-trace.tsv).

## Dedicated-slab patch follow-up

A private collaborator supplied a patch that routes `io_rsrc_node` fresh
allocations through a dedicated `kmem_cache`. The attachment was applied
unchanged to public v6.18-rc4 `6146a0f1dfae`; the resulting child was
`72461b3d32e3`. Six fresh boots compared unpatched, patched with default SLUB
merging, and the same patched binary with global `slab_nomerge`:

| workload | unpatched | patch default | vs unpatched | patch `slab_nomerge` | vs unpatched |
| --- | ---: | ---: | ---: | ---: | ---: |
| F0 4,096-slot first fill | 119.394 | 129.586 | `+8.536%` | 129.721 | `+8.649%` |
| 128-slot cold | 127.401 | 137.154 | `+7.656%` | 136.934 | `+7.483%` |
| 128-slot warm reuse | 108.677 | 108.261 | `-0.382%` | 108.463 | `-0.197%` |

The default patch cache was merged into `:A-0000032`; with `slab_nomerge` it
was an independent named cache. Their near-identical results show that
merging did not explain the roughly 8% cold slowdown. Warm reuse did not show
the slowdown because it obtained nodes from the per-ring cache rather than
the slab allocator.

A second four-boot experiment kept `slab_nomerge` on both sides and removed
only `SLAB_ACCOUNT` from the patched cache. The account commit was
`72461b3d32e3`; its direct child `82669ceb64b7` contained that one source-line
change:

| workload | with `SLAB_ACCOUNT` | without it | change |
| --- | ---: | ---: | ---: |
| F0 4,096-slot first fill | 130.326 | 118.099 | `-9.382%` |
| 128-slot cold | 137.338 | 125.207 | `-8.833%` |
| 128-slot warm reuse | 108.139 | 108.657 | `+0.479%` |

Both named caches had object size 24, alignment 32, order 0, 128 objects per
slab, and zero aliases. F0 account/no-account control drift was `-0.318%` and
`-0.044%`; drop-first remained `-9.375%`. This isolates the patch's added
cold cost to work enabled by `SLAB_ACCOUNT`, but does not assign the cost to
individual memory-cgroup instructions. Removing the flag is a diagnostic,
not a proposed fix, and the no-account point is not mixed with the unpatched
point from the separate six-boot sequence. See
[`uzair-patch1-followup.tsv`](uzair-patch1-followup.tsv).

## Revised v2 slab and bulk-refill patches

Uzair then supplied a two-patch v2 series. Patch 1 keeps the dedicated
`io_rsrc_node` slab but removes the unintended `SLAB_ACCOUNT` flag. Patch 2
refills the per-ring node cache in batches of 32 after a cache miss. Both
attachments were applied unchanged to public v6.18-rc4 `6146a0f1dfae`;
the local test commits were `da4febd3fde7` for patch 1 and `2359f858fa8d`
for patch 1+2.

The formal sequence used six fresh boots:

```text
unpatched-A -> patch1-A -> patch1+2-A -> patch1+2-B -> patch1-B -> unpatched-B
```

Each point retained the original 4,096-slot first-fill workload, 3 warm-up
rounds, 15 measured rounds, and actual `preempt=full`:

| role | ns/install | vs unpatched | vs patch 1 | control drift |
| --- | ---: | ---: | ---: | ---: |
| unpatched | 119.187760 | — | — | `+0.661%` |
| patch 1 | 118.406665 | `-0.655%` | — | `-0.171%` |
| patch 1+2 | 118.415243 | `-0.648%` | `+0.007%` | `-0.872%` |

All 90 measured primary rows passed the workload semantic checks, and the
maximum primary CV was `1.301%`. Patch 1 is therefore effectively neutral at
this boundary, while patch 2 has no measurable incremental benefit. A
drop-first sensitivity check gave the same interpretation. These current-base
numbers cannot be subtracted from the earlier v6.12.95-to-v7.1.3 `+15.602%`
release comparison because they use a different baseline and answer a narrower
patch question.

The secondary 128-slot cold diagnostic measured patch 1+2 `1.404%` slower
than patch 1, but its patch 1+2 control drift was `2.786%`. It is retained as
noisy diagnostic evidence and is not used to infer a slowdown. The warm
diagnostic also showed no bulk-refill benefit. Compact timing data, including
drop-first and both diagnostics, are in
[`uzair-v2-bulk-refill.tsv`](uzair-v2-bulk-refill.tsv).

A separate **untimed** probe established that the neutral timing did not come
from missing patch 2. Both kernels executed 73,985 `io_rsrc_node_alloc()`
calls. Patch 1 made 73,985 single-object slab allocations; patch 1+2 instead
made 2,313 bulk calls. Every bulk call requested and returned all 32 objects,
with no short or zero return. The 256-slot smoke cycle used exactly 8 calls,
and each of the eighteen complete 4,096-slot cycles used exactly 128. The
31-object difference between 74,016 bulk-returned objects and 73,985 consumed
nodes is the unused remainder of the source ring's initial refill. This proves
the intended bulk path ran correctly, but it does not explain why fewer
allocator calls did not reduce end-to-end first-fill time. See
[`uzair-v2-bulk-refill-mechanism.tsv`](uzair-v2-bulk-refill-mechanism.tsv).

## First-fill allocation-path diagnostics

Three consecutive diagnostics narrowed the original first-fill cost without
changing the formal `+11.621%` result.

First, an untimed trace on the exact `7029acd8` child observed 4,096
`io_rsrc_node_alloc()` calls. Exactly 32 of the workload PID's 40
`allocate_slab()` calls were dynamically nested under node allocation and all
used the same cache pointer. This matches 4,096 objects divided by 128 objects
per order-0 slab. A separate whole-process perf sandwich found the child at
`+6.999%` cycles and `+8.063%` instructions against the parent midpoint. The
L1D miss result was directional and LLC misses were inconclusive. These probes
prove that new slab backing is allocated and that total CPU work is higher;
they do not measure how much clean latency the 32 slab-page creations alone
explain.

Second, a diagnostic direct child of `7029acd8` preallocated and cached all
4,096 raw nodes during sparse-table registration. The later SEND_FD first fill
still made all 4,096 logical node-allocation calls, but an untimed gate found
zero `allocate_slab()` calls inside their dynamic scope. Clean fresh boots ran
`child A -> prefill -> child B`:

| point | ns/install | CV |
| --- | ---: | ---: |
| child A | `115.289095` | `1.084%` |
| diagnostic prefill | `104.245882` | `0.338%` |
| child B | `115.071126` | `1.723%` |

The child midpoint was `115.180111 ns/install`; prefill reduced the timed cost
by `10.934228 ns/install` (`-9.493%`). Child endpoint drift was `-0.189%`, and
drop-first was `-9.456%`. This establishes that the raw per-object allocation
and zeroing path accounts for most of the measured first-fill overhead in this
diagnostic. It does **not** isolate slab-page creation from the other 4,096
`kzalloc()` operations, and it does not show lower total cost: registration
time and retained memory were moved outside the timed window and were not
measured. The variant is diagnostic, not an upstream fix.

Third, a narrower diagnostic primed slab backing while retaining the 4,096
per-object allocations in the timed first fill. Registration allocated 4,129
objects through the real `io_rsrc_node_alloc()` callsite, returned 4,096 to
the allocator, and retained 33 page anchors. The untimed probe observed 32
`allocate_slab()` calls during registration. Timed first fill still made
4,096 `io_rsrc_node_alloc()` and 4,096 per-object kmalloc calls, but made zero
`allocate_slab()` calls. Clean fresh boots ran
`child A -> slab-prime A -> slab-prime B -> child B`:

| point | ns/install | CV |
| --- | ---: | ---: |
| child A | `115.055192` | `0.943%` |
| slab-prime A | `116.418701` | `1.632%` |
| slab-prime B | `115.258854` | `1.205%` |
| child B | `114.494531` | `0.345%` |

The child midpoint was `114.774862 ns/install`; the slab-prime midpoint was
`115.838778 ns/install`. The diagnostic measured `+0.927%`
(`+1.063916 ns/install`), so it did not improve timed first fill. Child and
slab-prime endpoint drifts were `-0.487%` and `-0.996%`; drop-first was
`+1.050%`. This more narrowly rejects fresh slab-page creation as a material
explanation under the diagnostic. It does not measure a pure per-page cost,
because registration also allocated and touched objects and retained anchors.

Together, the full-prefill and slab-prime results leave repeated per-object
allocation, zeroing, and first-touch or locality effects as the useful search
space. They do not show which of those costs is largest, and neither variant
is a total-cost fix. Compact records for all three steps are in
[`first-fill-allocation-diagnostics.tsv`](first-fill-allocation-diagnostics.tsv).
The exact diagnostic source deltas are
[`full-prefill`](../reproducer/0001-diagnostic-prefill-sparse-node-cache.patch)
and
[`slab-prime`](../reproducer/0002-diagnostic-prime-node-slab-backing.patch).

## v3 prefill and lifecycle follow-up

The [consolidated v3 page](uzair-v3-prefill/README.md) preserves both
completed v6.18-rc4 U/B/C matrices without replacing the earlier evidence.
First fill improves by `9.791%` against unpatched and `8.819%` against
0001+0002. Same-ring 4,096-file remove/refill improves by
`18.820% / 18.584%`; a 64-file reuse control is effectively unchanged.
Registration through first fill does not improve. Low-use/small-table
failures and retained-memory costs are reported, not filtered out.
All 28 metric summaries, 900 measured groups, individual-tail summaries,
independent mechanism checks, and auxiliary sources are kept together.

## Platform and scope

The physical machine was an Intel Core i7-12700KF system with 32 GiB RAM. The
single process was pinned to P-core logical CPU 2, the scaling governor and
EPP were `performance`, and Turbo was disabled.

This evidence concerns fixed-file registration/update through
`IORING_MSG_SEND_FD`. It is not a claim about ordinary io_uring read/write
submission, application performance, or every fixed-file update pattern.
