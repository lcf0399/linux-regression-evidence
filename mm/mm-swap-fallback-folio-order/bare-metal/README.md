# Fixed MM-tree A/B/A data

All numbers here belong to the same 2026-10-15 boot sequence:
`7.3.0-rc5-mm-swpbase1` → `7.3.0-rc5-mm-f02rcl2` → the same baseline.
Do not combine them with the earlier 6.18.38 prototype results.

- `builds.tsv`: exact source, compiler, image and configuration identities.
- `baseline.config`, `prototype.config`: actual configurations; only the
  release suffix differs.
- `samples.tsv`: **all 132 invocations**, including 24 explicitly marked
  warmups. A formal invocation averages four internal cycles.
- `cycles.tsv`: individual worker-cycle clocks and page/data checks,
  retained for independently reconstructing the sample CPU metrics.
- `summary.tsv`: all five recorded metrics, all four conditions, means,
  return drift and maximum within-point descriptive CV (`pstdev / mean`,
  matching the original run analysis). No negative control is
  omitted. `change_pct < 0` means lower cost.

For each invocation:

```text
api_cpu_ns_per_page = sum(pageout_cpu_ns + refault_cpu_ns) / (4 * 2048)
CPU us per 8 MiB    = api_cpu_ns_per_page * 2048 / 1000
baseline mean      = mean(all 18 formal A + return-A invocations)
change percent     = 100 * (prototype mean / baseline mean - 1)
return drift       = 100 * (return-A mean / initial-A mean - 1)
```

The two-worker metric is a **sum of CPU time**, not concurrent elapsed
latency. All workers together process 8 MiB in each cycle. PAGEOUT return
is not an assertion that disk I/O has completed; cache readiness is checked
outside the measured call before the timed refaults.

`api_worker_wall_ns_per_page` sums worker wall intervals; it is not
throughput. `barrier_ns_per_8MiB` includes a fixed 20 ms wait, page/data
checks and discard, but excludes initial filling/collapse. It is not a
whole-application metric. Every recorded final deferred swap charge is
zero; cleanup itself is not included in the primary timing.

The samples were collected without diagnostic probes. Page-order/BIO/IOVA
observations came from **separate non-timing runs** and are not subtracted
from these clocks. All formal sample ranges and controls are retained.
The two THP groups have non-overlapping baseline/prototype ranges;
the base-page groups overlap. One A/B/A block is not an estimate of
cross-machine variability.

The standalone [verifier](../reproducer/verify_results.py) recomputes the
CPU metrics and summary directly from these TSV files, without the private
experiment framework.
