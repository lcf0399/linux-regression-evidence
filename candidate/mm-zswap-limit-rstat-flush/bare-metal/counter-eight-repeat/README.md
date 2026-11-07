# Separate eight-worker repeat — November 6, 2026

A fresh matched MM baseline / counter derivative / baseline series completed
54 clean calls, 12 separate warmups and18 admission checks. It retains the
same 32 MiB total, eight fixed P-cores, kernel pair and timed worker logic.
The new program waits for preparation-time memory.stat convergence before
accepting the state; it does not force a statistics flush or put that wait
inside PAGEOUT. All 54 measured calls passed the preparation check on the
first read.

- `samples.tsv`: all 54 measured observations, grouped by case and point.
  `repetition` preserves order within each case, not global inter-case order.
  Timing columns are microseconds; the worker phase CPU is summed across eight workers.
- `summary.tsv`: means, sample CVs, change versus the mean of both controls,
  and return drift for PAGEOUT, restore/verification, whole-program CPU and wall time.
- `identity.json`: source/binary/patch identity and the hash of the independently
  checked local source summary. No host identity, mail or credentials is exported.
- [Measured source](../../reproducer/zswap_case_statwait.c) includes the same
  [helper](../../reproducer/peak_state.c) as the earlier batches. Compile with
  the flags and dedicated-host prerequisites in the [reproduction guide](../../reproducer/README.md).

Finite-limit PAGEOUT improves in all nine derivative samples, but per-boot CVs
remain 37.16%/57.62%/40.91%. Unlimited and restore distributions overlap and have
unfavorable mean directions. These are descriptive effects, not stable universal
percentages or proof of no concurrency cost. The original failed preparation
attempt and its valid prefix remain local, separate from this fresh batch.
Warmups, detailed admissions and raw outputs remain in that local archive too.
The earlier three public datasets are unchanged and are not pooled with this one.
