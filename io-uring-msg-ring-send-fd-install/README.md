# io_uring MSG_RING SEND_FD fixed-file installation

This bundle documents a narrow io_uring registration/update slowdown. The
workload uses `IORING_OP_MSG_RING` with `IORING_MSG_SEND_FD` to install one
source fixed file into empty slots of a second ring's sparse fixed-file table.
It is a focused synthetic microbenchmark, not an application benchmark or a
claim about the ordinary io_uring read/write fast path.

## Primary result

The formal evidence is an exact direct-parent comparison around
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
(`io_uring/rsrc: get rid of per-ring io_rsrc_node list`):

| point | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | `102.476` | `0.705%` |
| child | `114.441` | `0.289%` |
| parent B | `102.576` | `0.340%` |

The child was `11.621%` slower than the parent midpoint. Drop-first was
`11.649%`, parent drift was `0.097%`, and all 45 measured rows passed semantic
checks. The same workload binary, normalized config, build controls, CPU 2,
and actual runtime `preempt=full` were used at all three fresh-boot points.

The change gives each fixed-file slot an independent resource node, removing
per-ring serialization and reclamation stalls. This bundle therefore records
a measured registration/update trade-off and does not recommend reverting the
correctness/scalability change.

## v3 patch validation (2026-09-10)

The unchanged three-patch v3 series was tested on exact public v6.18-rc4.
Original first fill was `107.731 ns/install`, `9.791%` below unpatched and
`8.819%` below 0001+0002. A separate same-ring 4,096-file remove/refill
interval improved by `18.820%` / `18.584%`, respectively, with independent
counts showing that the larger cache avoids repeated allocation.

Registration through first-fill completion did not improve (`+1.144%` in
the original auxiliary and `+1.637%` with paced new rings). Low-use and
small-table timing failures remain visible, alongside 4,032 idle nodes
after registering 4,096 slots and using 64. Asynchronous exit overlaps
iterations even without the patches; its role in earlier timing noise
was not established. See the [v3 result page](bare-metal/uzair-v3-prefill/README.md)
for all 28 timing metrics, mechanism checks, identities, and auxiliary
sources. These are not complete-lifetime or generic I/O gains.

## Evidence chain

| question | result | scope |
| --- | --- | --- |
| Does the release-level signal reproduce? | Linux 6.13 vs 6.12: `+10.747%`; Linux 7.1.3 vs 6.12.95: `+15.602%` | supporting matched endpoint checks |
| Is it confined to 4,096 slots? | 64–4,096 slots: `+8.399%` to `+11.889%` | scope check; smaller points were noisier |
| Does the workload hit the new path? | 128 fixed-file installs changed nested `io_rsrc_node_alloc()` from `0` to `128` | untimed direct-hit trace |
| Why did the later node cache not close first-fill cost? | v7.1.3 same-ring reuse changed `137.789` to `109.643 ns/install` (`-20.427%`) | cache helps reuse; a new ring starts with an empty cache |
| Did a dedicated slab help? | initial private patch slowed cold first fill about 8%; deleting unintended `SLAB_ACCOUNT` removed that added cost | diagnostic only; accounting semantics matter |
| Did revised 32-object bulk refill help? | patch 1: `-0.655%`; patch 1+2 vs patch 1: `+0.007%` | clean current-base timing was neutral; an untimed probe proved full bulk execution |
| Are new slab pages created during first fill? | 4,096 node allocations contained exactly 32 nested `allocate_slab()` calls | mechanism proof, not clean latency attribution |
| Do those new slab pages explain the gap? | priming slab backing removed all 32 new-slab calls from timed first fill but measured `+0.927%` | no improvement; 4,096 per-object allocations remained |
| Where is most timed cost? | moving all 4,096 raw node allocations/zeroing to registration reduced first-fill by `10.934 ns/install` (`-9.493%`) | diagnostic prefill; registration time and retained memory were not measured |

The two prefill diagnostics separate these costs more clearly. Slab priming
removed new-page creation from the timed region but kept all 4,096 per-object
allocations and did not improve latency. Full prefill moved all raw allocation
and zeroing out of the timed region and was `9.493%` faster. This narrows the
remaining investigation to repeated per-object allocation, zeroing, and
first-touch or locality effects, without proving which one is largest. Neither
diagnostic shows a total-cost win because registration work and retained
memory were outside the primary measurement.

Detailed statistics, scope limits, path counts, and the compact allocator
diagnostics are in [`bare-metal/`](bare-metal/README.md). Both exact diagnostic
source deltas are included for reproducibility and are explicitly not upstream
fix proposals.

## Layout

- [`bare-metal/`](bare-metal/README.md): detailed exact A/B evidence, scope
  checks, cache/allocation diagnostics, collaborator-patch results, compact
  identities, and selected raw rounds;
- [`reproducer/`](reproducer/README.md): exact experiment source, readable
  F0-only source, cold/warm helper, validator, runners, and diagnostic prefill
  source deltas;
- [`upstream-status/`](upstream-status/): introducing thread, later cache
  change, and dated source audit;
- `email/`: local correspondence and sending notes; ignored by Git and not
  part of the public evidence bundle.
