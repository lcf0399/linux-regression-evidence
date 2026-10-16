# Dedicated slab and batch 32 tests

Uzair's old patches 1+2 keep the idle-node cache limit at 128 and refill on a
cache miss from a dedicated slab, in batches of 32. The matched baseline is
public v6.18-rc4 `6146a0f1dfae5d37442a9ddcba012add260bceb0`; the local test
commit for the unchanged patch pair is `2359f858fa8db024958efc2eb7ac43a9002b44fe`.
Private patch correspondence is not redistributed here. This is not the
shared-kmalloc/128 prototype and not a direct comparison against it.

## Small management and TCP results

Measured 2026-09-29, unpatched A → patches 1+2 → unpatched B. Positive changes
mean the patched version took longer. Values are microseconds per batch.

| Case | Unpatched midpoint | Patched | Change | Stability |
| --- | ---: | ---: | ---: | --- |
| Register and first fill 4 | 7.433 | 8.755 | — | fail |
| Register and first fill 64 | 16.500 | 17.355 | +5.182% | pass |
| Register and first fill 256 | 40.439 | 41.600 | +2.870% | pass |
| Remove and refill 4 | 1.048 | 1.055 | +0.616% | pass |
| Remove and refill 64 | 9.288 | 9.292 | +0.038% | pass |
| Remove and refill 256 | 39.175 | 39.492 | +0.809% | pass |
| TCP 4, 1 exchange, fresh rings | 440.253 | 439.522 | — | fail |
| TCP 4, 16 exchanges, fresh rings | 630.976 | 628.560 | −0.383% | pass |
| TCP 64, 1 exchange, reused rings | 1,210.673 | 1,219.810 | +0.755% | pass |
| TCP 64, 16 exchanges, reused rings | 4,019.003 | 4,042.051 | +0.573% | pass |
| TCP 256, 1 exchange, reused rings | 4,614.828 | 4,635.117 | +0.440% | pass |
| TCP 256, 16 exchanges, reused rings | 16,483.919 | 16,553.654 | +0.423% | pass |

The pair did not show a clear benefit. The 64-entry combined management
interval was 5.18% slower; this is the pair's effect, not an isolated
batch-size effect. All five stable TCP totals changed by less than 1%.
Unstable cases are unresolved, not neutral results.

## Why fewer allocation calls did not guarantee a gain

Independent probes confirmed that batch refill ran. For 256 entries followed
by four remove/refill cycles, both versions allocated 768 target nodes; the
patch replaced 768 single-object allocation calls with 24 batches of 32.
For 64 entries, neither version needed further allocation after the first
fill. Four-entry first fill obtained 32 objects with the patch, leaving some
unused. These counts cover target installation, not all allocations in the
process.

Batching does not eliminate per-object processing or file installation. The
earlier patch-1 versus patch-1+2 comparison at 4,096 entries also showed no
incremental first-fill improvement: 118.407 versus 118.415 ns/install.
However, the specific cause of the 64-entry slowdown has not been isolated.
These negative results do not prove that all other bulk-allocation designs
must be ineffective.

## Earlier large-batch controls

The preserved [v3 experiment](../uzair-v3-prefill/README.md) also compared the
unpatched kernel with patches 1+2, separately from the full three-patch v3:

| 4,096-entry interval | Unpatched us | Patches 1+2 us |
| --- | ---: | ---: |
| Registration plus first fill | 479.708 | 479.591 |
| Same-ring removal plus refill | 712.451 | 710.384 |

Both were effectively unchanged. These intervals exclude network I/O and
are not complete ring lifetimes. The [earlier batch-path checks](../history/uzair-v2-bulk-refill-mechanism.tsv)
and [patch-1 incremental comparison](../history/uzair-v2-bulk-refill.tsv)
remain available without repeating their full narrative here.

## Data and scope

[timing.tsv](timing.tsv) retains all 9,828 trial rows, including warmups.
[results.tsv](results.tsv) retains all 54 phase metrics and failed stability
checks. [provenance.json](provenance.json) contains identities and independent
numeric probe records. See the [shared methods](../README.md) and
[reproducers](../../reproducer/README.md). The TCP tests use actual sockets
and payloads, but do not establish that real applications commonly replace
connections in these whole batches.
