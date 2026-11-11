# Separate eight-worker 256 MiB supplement — November 10, 2026

This fixed MM baseline / measured derivative / baseline batch processes 32 MiB
per worker, 256 MiB total, on CPUs 0,2,4,6,8,10,12,14; controller CPU 18.
It is not additional data for any old 32 MiB batch. The MM builds and patch are
the same as [counter-mm-pair](../counter-mm-pair/README.md). LZO/zsmalloc,
full preemption and microcode 0x3e matched at all three endpoints.
Each point has two warmups and nine measured repetitions per condition:
54 measured calls, 12 separate warmups and 12 non-timing state/path admissions.
Content, 65,536 successful stores, no memory high/max/OOM events, final compressed
object release, matched runtime and restoration passed raw-output replay.

- `samples.tsv`: all 54 measured observations. Timings are in microseconds
  except the explicitly named nanoseconds-per-page column. PAGEOUT/reaccess CPU
  is summed across workers; whole-program CPU includes the controller and waited
  children. `repetition` retains order within each case, not global inter-case order.
- `summary.tsv`: means, sample CVs, descriptive change versus the mean of both
  controls and return drift, independently recomputed from the public observations.
- `admissions.tsv`: all 12 non-timing checks. `NA` is unobserved without probes,
  not zero. Finite forced flushes fall from 65,536 to zero; unlimited remains zero.
- `identity.json`: exact measured source/binary/patch and local frozen-summary
  identities, without private host paths, mail or machine identifiers.
- [Measured source](../../reproducer/zswap_case_scale256.c) includes the unchanged
  [helper](../../reproducer/peak_state.c); its [diff](../../reproducer/scale256-source.diff)
  is against the separately measured 32 MiB stat-wait source.

Private cgroup limits are 2 GiB memory and 1 GiB swap, with 64 MiB finite zswap
quota or `max`. Finite compressed occupancy is 18.7518–18.7520 MiB, below quota;
these are unpressured checks, not near-limit or production-scale results.
The finite PAGEOUT benefit direction is clear, but +5.10% baseline return drift
limits precision of the descriptive −84.04% mean. Unlimited PAGEOUT +1.17%,
with +0.97% return drift, does not establish a stable penalty or equivalence.
Reaccess can hit swap cache and does not isolate cold decompression.
Older noisy/adverse 32 MiB observations remain intact and are not pooled here.

The exporter reran the frozen read-only raw parser and required exact equality
with the retained independent summary before selecting these columns. Warmups,
raw outputs, full runtime snapshots and transport/recovery records stay local.
Run `python3 ../../reproducer/verify_results.py` for offline public-data checks;
this does not rerun the workload or validate untested concurrency conditions.
