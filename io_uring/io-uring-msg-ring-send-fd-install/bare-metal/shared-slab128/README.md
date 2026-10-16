# Shared kmalloc and batch 128 prototype

Measured 2026-10-01. **This independent diagnostic prototype showed no clear
benefit in the tested management and TCP cases.** Registration plus first fill
was 9.61% slower at 64 entries and 4.42% slower at 256. It is an implementation
of the suggested fixed-limit shared-allocation approach, not a patch supplied
or approved by Gabriel or Uzair, and not a proposed production fix.

## Design and exact patch

The [tested patch](shared-bulk128.patch) applies to public v6.18-rc4
`6146a0f1dfae5d37442a9ddcba012add260bceb0`. Its SHA-256 is
`b472aa07d31dec55b4823c3e43b1123d88618aa48b89f9dbc5ead47539f985d4`.
The baseline plus this patch is the tested source, not an unmodified upstream
tree. The two roles use matched canonical configuration and GCC 15.2.0.

- Keep the limit at **128 idle resource nodes**, not 128 file-table slots.
- Do not prefill during registration. First try the existing node cache.
- On an empty cache, request 128 nodes from the shared kmalloc allocation
  mechanism. Return one and put the other 127 through the existing cache-put
  checks. If bulk allocation fails, fall back to the original single allocation.
- Do not create a dedicated slab or change file-slot capacity. Keep node
  initialization, references, file release and ring cleanup unchanged.

The base did not provide the needed shared-kmalloc bulk entry point. The
diagnostic therefore adds `kmalloc_bulk_noprof()` and passes the requested
object size through internal bulk allocation. It retains allocation hooks,
accounting, KASAN/KFENCE and requested-size handling instead of disabling
those checks. Existing `kmem_cache_alloc_bulk()` users retain their cache
object-size argument. This is more than changing the old batch-32 constant.

Shared kmalloc does not prove that both paths used the same physical slab:
randomized kmalloc cache selection remains enabled. Full kernel compilation
and the measured checks passed; that does not validate every allocator
configuration, concurrent interaction or low-memory condition.

## Matched performance results

Unpatched A → prototype → unpatched B, after separate non-timing qualification.
Values are microseconds per batch. Positive changes mean the prototype took
longer. Both full and drop-first stability checks are required.

| Case | Unpatched midpoint | Prototype | Change | Stability |
| --- | ---: | ---: | ---: | --- |
| Register and first fill 4 | 7.537 | 9.974 | — | fail |
| Register and first fill 64 | 16.684 | 18.287 | +9.613% | pass |
| Register and first fill 256 | 40.154 | 41.928 | +4.418% | pass |
| Remove and refill 4 | 1.051 | 1.052 | +0.110% | pass |
| Remove and refill 64 | 9.291 | 9.302 | +0.112% | pass |
| Remove and refill 256 | 39.198 | 39.643 | +1.135% | pass |
| TCP 4, 1 exchange, fresh rings | 437.056 | 439.569 | — | fail |
| TCP 4, 16 exchanges, fresh rings | 621.026 | 629.222 | +1.320% | pass |
| TCP 64, 1 exchange, reused rings | 1,209.329 | 1,213.609 | +0.354% | pass |
| TCP 64, 16 exchanges, reused rings | 4,026.182 | 4,026.378 | +0.005% | pass |
| TCP 256, 1 exchange, reused rings | 4,613.388 | 4,639.623 | +0.569% | pass |
| TCP 256, 16 exchanges, reused rings | 16,449.292 | 16,548.434 | +0.603% | pass |

The four-entry management result failed control return drift (−2.354%;
drop-first −2.462%). Four-connection/one-exchange TCP failed maximum CV
(3.171%; drop-first 3.193%). Neither is used for a performance claim.
Of 54 phase metrics, 39 were stable and none met the predefined 5% benefit
threshold. The other 15 are unresolved, not evidence of no benefit.

These are separate matched comparisons from the old batch-32 tests. Do not
subtract their percentages to rank the two implementations.

## Allocation and retained objects

Independent probes, not the timed runs, observed:

| First-fill demand | Unpatched objects allocated | Prototype objects and batches | Prototype idle nodes after fill |
| ---: | ---: | ---: | ---: |
| 4 | 4 | 128 in one batch | 124 |
| 64 | 64 | 128 in one batch | 64 |
| 256 | 256 | 256 in two batches | 0 |

The node requests 24 bytes and used a 32-byte allocation bucket. The unused
objects at demands 4 and 64 account for 3,968 and 2,048 bytes respectively.
These are object allocations, **not whole slab-page or system resident-memory
measurements**. The idle cache stayed within 128 and all tracked nodes were
returned and freed by the end of each qualification case.

At 256 entries, first fill plus four remove/refill cycles used nodes 1,280
times. Both roles allocated 768 new objects; the prototype used six batches
instead of 768 single-object calls. At 4 and 64 entries, later reuse needed
no new target objects in either role. Cache capacity therefore does not
equal allocation traffic or the number of installed files.

## What the cause review establishes

The stable first-fill-only interval increased by 1.835 us at 64 entries
(14.473 → 16.308 us) and 1.771 us at 256 (38.008 → 39.779 us). At 256, both
roles allocated exactly 256 objects, so unused extra objects cannot explain
the whole slowdown.

Source and static disassembly of the tested image show that the 127-object
cache-put loop remains, including capacity checks, count updates and pointer
stores. The bulk entry point also retains per-object processing and a loop
over allocation trace checks. Fewer allocation calls do not remove that work.
These are plausible overheads, **not separately measured causal contributions**.

KASAN was disabled in both tested configurations; it cannot explain the
measured difference. `CONFIG_INIT_ON_ALLOC_DEFAULT_ON=y` and randomized
kmalloc caches remained enabled. A zero `node_cache.init_clear` does not
establish that the allocator performs no initialization. Allocator state and
code-generation effects have not been independently isolated.

## Validation and limits

All 24 non-timing cases and 108 node-inventory snapshots passed. Each snapshot
checks `idle + active = allocated - freed`; final counts require every node
use to be returned and every tracked allocation to be freed. CQE visibility
alone is not treated as proof of immediate node return.

Local extracted-function tests exercised bulk failure, single-allocation
failure and cache rejection with ASan/UBSan. They do not replace live-kernel
fault injection, lockdep or exhaustive concurrency testing. The local test
summary and numeric runtime counts are in [provenance.json](provenance.json).
Early probe-development attempts stopped before timing; the final run used
explicit kernel-pointer reads and conservation checks. Those failed attempts
were not reclassified as passes, and clean timing was not repeated to pass
stability gates.

[timing.tsv](timing.tsv) and [results.tsv](results.tsv) preserve all 9,828
trial rows and all phase metrics. The [shared methods](../README.md) define
the management and TCP boundaries. The [reproducer instructions](../../reproducer/README.md)
and [read-only verifier](../../reproducer/verify_followups.py) are self-contained.
No new parameter search or larger-cache recommendation follows from this test.
