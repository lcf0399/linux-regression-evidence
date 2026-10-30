# MMTests pipe-handoff throughput and delayed-requeue accounting

**Origin:** this report came from reproducing the existing MMTests
`will-it-scale/context_switch1` benchmark, not from our source/call-graph
or model-generated workloads. It is recorded separately from that method's
discovery results.

Formal evidence bundle, promoted on 2026-10-30. The report is prepared for
upstream; promotion does not mean it has been sent, accepted or fixed.
Mike Galbraith previously identified the same commit in a different wakeup
test; this is complementary throughput and attribution evidence.

## Main results

The benchmark exchanges single-byte messages over two pipes between each
worker and its helper. Native CORE affinity is retained on eight P-cores
of an i7-12700KF, with both SMT siblings available. Above eight workers,
cores are reused. Both thread and process modes were tested.

Comparison: **v7.1.3 → v7.3 → v7.1.3**. Values below are throughput changes
relative to the average of the two baseline means; negative means slower.

| Workers | Thread mode | Process mode |
| --- | ---: | ---: |
| 1 / 2 / 4 / 6 / 8 | −0.70% to −4.52% | −1.32% to −4.59% |
| 11 | −10.54% | −9.96% |
| 15 | −15.69% | −14.82% |
| 20 | −14.81% | −14.43% |
| 26 | −12.69% | −13.55% |
| 33 | −8.75% | −7.33% |
| 39 | −7.39% | −6.46% |
| 40 | −7.19% | −6.51% |

At 15 thread workers: **10,875,243 → 9,168,152 → 10,872,945 handoffs/s**,
about 1.71 million fewer handoffs/s on v7.3. Maximum within-boot CV is
2.01%; maximum baseline drift is 1.39%.

A separate matched v7.2 comparison reverses only
[`333238da9a19`](attribution/333238da9a19.patch):

| Thread workers | v7.2 A | v7.2 with diagnostic revert | v7.2 B |
| --- | ---: | ---: | ---: |
| 2 | 1,178,295 | 1,174,770 | 1,182,545 |
| 15 | 9,413,892 | 10,695,877 | 9,409,225 |

Units: handoffs/s. At 15 workers the revert improves throughput by **13.65%**
(about 1.28 million handoffs/s); baseline drift is −0.05% and maximum CV
0.11%. The 2-worker control changes by −0.48%.

## Attribution

The commit updates the current entity before calculating a delayed entity's
lag. This accounting is necessary; the revert is a diagnostic, not a fix.

Separate diagnostics use the same 15 post-warmup seconds as the throughput
window:

- At 15 workers, kernel instructions/handoff rise from 8,338 on v7.1.3 to
  10,423 on v7.3 (+25.00%). The matched v7.2 revert reduces 10,239 to 8,355
  (−18.40%). Context switches remain about one per handoff.
- Entry probes observe about 1.0 delayed requeue per 100 handoffs at
  2 workers and 92.6 at 15 workers on v7.2, almost every time with the added
  `update_curr()`. The revert removes that call and also changes the
  15-worker requeue frequency to 55.3 per 100 handoffs.
- CPU-location samples show the same broad placement pattern across the
  compared kernels.

The evidence supports additional work in the delayed-requeue path.
Probe counts are diagnostic observations, not exact uninstrumented frequencies
or exclusive function timings. The full relationship between eligibility,
placement and requeue frequency has not been reconstructed.

## Reproduction and data

[Native reproduction instructions](reproducer/native-core.txt) contain the
pinned benchmark/kernel revisions, build commands, affinity and runtime settings.
The benchmark loop is unchanged. After a warmup run, each condition has nine
measured runs per boot; each run excludes six built-in warmup intervals and
measures fifteen intervals. Kernel builds use GCC 15.2.0, full preemption,
powersave/balance_performance and Turbo enabled.

- [702 unique clean observations](bare-metal/native-core-measurements.csv):
  648 scale-scan and 54 matched-attribution observations; 18 of the latter
  reuse a previously validated baseline once.
- [Compact results and identities](bare-metal/native-core-summary.json).
- [Dated upstream check and reporting state](upstream-status/README.md).

The throughput, PMU and entry-probe results are separate runs. Fixed-duration
throughput does not measure application latency or end-to-end application gain.

## Earlier related experiments

The older custom-futex workload and single-logical-CPU conditions are not the
MMTests report above and are not pooled with it. Historical custom-futex
[measurements](bare-metal/measurements.tsv), [summary](bare-metal/summary.json),
[build identity](bare-metal/identity.json), [probe records](bare-metal/independent-probes.json)
and [source](reproducer/weighted_payload.c) remain available. Their power
settings differ from the native benchmark batch.
