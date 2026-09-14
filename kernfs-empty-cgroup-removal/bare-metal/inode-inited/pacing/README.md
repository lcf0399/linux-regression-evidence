# INODE_INITED: warmup and pacing diagnostic

2026-09-14. One completed, bounded four-boot diagnostic: 216 invocation
samples and 6,912 measured `mkdir() -> stat() -> rmdir()` sequences.
The patch saved **2.10–2.58 µs per removal** across all six conditions.
With 16 warmups and continuous operation, the full sequence was
**5.77–8.76% shorter**, with maximum CV 1.76% and repeat-boot drift below 2%.

This is separate from the [original and 128-operation follow-up](../README.md).
The earlier noisy whole-sequence result is retained, not replaced or pooled.
No application-level benefit or complete concurrency-safety claim is made.

## Fixed comparison and timing

- A: `2f0c1cf72f4682178506f513bbf015e591b1aa4a` (7.3-rc2) plus Shakeel's
  file-handle decoding read-lock fix. B: A plus T.J.'s unchanged INODE_INITED
  changes. [Shared build identity and patches](../README.md#exact-comparison).
- Same installed kernels, i7-12700KF, CPU0, sched_ext disabled, full preemption,
  performance governor and Turbo disabled. [This run's identity](identity.json).
- Fresh boots: A1 → B1 → B2 → A2. Every boot includes all six conditions,
  with nine invocations per condition. Each invocation has 32 measured
  sequences, with 0 or 16 warmups and 0, 50 or 200 ms requested pauses.
- Counts and conditions were fixed before timing. The change from 128 to 32
  bounded the diagnostic's duration; it was not a post-result sample filter.
- Pauses occur **before** the enclosing sequence timer. Reported sequence
  latency already excludes them. It includes clock reads and userspace glue,
  but not parent setup, post-removal checks or all deferred reclamation.
- The separate `workload_wall_per_cycle_ns` field includes pauses/checks.
  It is not used to compare kernel operation cost across pacing conditions.

Continuous means no added pause between operations within an invocation.
The runner still waits 45 seconds after boot and 2 seconds between invocations.
No-warmup does not guarantee a cold cache: parent setup still executes.

## Removal latency

Each A/B column lists A1/A2 or B1/B2, in microseconds. Values are medians of
nine invocation means. Reduction ranges include all four B/A endpoint pairs,
not confidence intervals or a selection of the best pair.

| Warmups / pause ms | A, µs | B, µs | Reduction | Maximum CV |
| --- | --- | --- | --- | --- |
| 0 / 0 | 10.125 / 10.018 | 7.852 / 7.819 | 21.62–22.77% | 0.94% |
| 16 / 0 | 9.758 / 9.755 | 7.596 / 7.544 | 22.13–22.69% | 0.93% |
| 0 / 50 | 18.229 / 18.126 | 15.854 / 15.694 | 12.53–13.91% | 1.76% |
| 16 / 50 | 18.129 / 18.024 | 15.926 / 15.789 | 11.64–12.91% | 1.67% |
| 0 / 200 | 18.289 / 18.236 | 15.822 / 15.787 | 13.24–13.68% | 2.22% |
| 16 / 200 | 18.171 / 18.332 | 15.846 / 15.756 | 12.79–14.05% | 2.45% |

All removal conditions pass the preset CV ≤3% and repeat-boot drift ≤2%
checks. Dropping the first whole invocation per boot as a sensitivity check
also retains at least 5% improvement. No original samples are removed.

Pauses raise the removal baseline from about 10 to 18 µs, while the absolute
saving remains 2.10–2.58 µs. That produces a smaller percentage improvement.
Increasing the requested pause from 50 to 200 ms does not double the baseline
again. Pacing checks whether the benefit persists with less frequent operations;
these intervals are not recommended application settings.

## Complete sequence latency

| Warmups / pause ms | A, µs | B, µs | Measured reduction | Maximum CV |
| --- | --- | --- | --- | --- |
| 0 / 0 | 30.733 / 30.628 | 28.208 / 28.302 | 7.60–8.21% | 4.77% |
| 16 / 0 | 30.074 / 30.652 | 27.966 / 28.338 | 5.77–8.76% | 1.76% |
| 0 / 50 | 81.157 / 81.666 | 78.717 / 80.107 | 1.29–3.61% | 1.59% |
| 16 / 50 | 82.663 / 81.111 | 80.305 / 80.575 | 0.66–2.85% | 1.38% |
| 0 / 200 | 83.029 / 82.504 | 81.264 / 81.945 | 0.68–2.13% | 1.27% |
| 16 / 200 | 82.274 / 82.081 | 80.443 / 80.613 | 1.79–2.23% | 1.53% |

The 16/0 condition passes the stability checks: A/B drift is +1.92%/+1.33%,
and drop-first still gives 5.60–8.64% improvement. This does not establish
that reducing the invocation length caused the lower variability.
The 0/0 condition remains unstable; its percentage is descriptive only.
All paced full-sequence conditions pass stability checks but remain below
the preset 5% effect threshold, which does not mean zero benefit.

Individual mkdir/stat results and initial-operation effects remain in the data.
For example, the 16/0 mkdir A drift is +2.78%; stat maximum CV is 5.50%.
Their separate changes cannot be reliably quantified even though the enclosing
sequence is stable. First-operation comparisons are noisy in all six cases.
Initial, first-four and remaining-28 observations overlap with the full sample;
they are not additional independent samples.

No cache misses or operation-scoped CPU frequencies were measured. This run
does not identify the separate effects of cache/TLB state, frequency response
or deferred cleanup. Lower CV is not proof of smaller absolute jitter or a
more representative application workload.

## Mechanism and reproduction

Separate preflight: 24 smoke/trace invocations, each with four operations and
no warmups. The 48 traced operations retain 26 kernfs nodes per removal;
`ilookup()` falls from 26 to 1. First stat adds one write-lock/unlock pair.
`kernfs_init_inode` is folded into `kernfs_get_inode`; a zero standalone count
does not mean the initialization path was missed. [Counts](mechanism.json).
Timing ran without tracing, and generic was restored after both phases.

The [source](reproducer/control.c) is byte-identical to the tested build input.
Only the accepted pacing value and schema differ from the prior auxiliary
source; syscall order and timers are unchanged. Shared headers and patches
are hash-pinned in [provenance](provenance.json). Build with `make` from
`reproducer/`. On a dedicated matching host, the continuous warm condition is:

```sh
sudo env -u CG_GRAPH -u CG_TRACE CG_SCX_DISABLED=1 CG_CYCLE_PACE_MS=0 \
  ./build/control unused leaf 0 0 0 0 32 16
```

Use `CG_CYCLE_PACE_MS=50` or `200` for pacing and final argument `0` for
no warmup. Commands create a private cgroup subtree; do not run them on shared
production hosts. Rebuilding creates a new binary identity; record its hash.

## Offline verification

[Ordered samples](measured-rounds.tsv) contain 216 invocations with 18 metric
values each. All six conditions and noisy results are retained. The
[summary](summary.json) stores the four primary metrics; the verifier also
recomputes every initial/tail/loop metric from the table.

```sh
python3 -B verify.py
python3 -B verify.py --all-metrics
```

This checks hashes and independently recomputes all 432 groups, CVs, repeat-boot
drift and endpoint/drop-first comparisons. Export validation replayed the full
local timing and trace records before producing this compact bundle. Private
mail, credentials, raw environment logs and raw traces are excluded.
