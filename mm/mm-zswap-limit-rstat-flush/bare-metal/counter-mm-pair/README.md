# Matched MM counter evaluation

`00-baseline → 01-prototype → 02-baseline`, on base commit
`2967f3ccdb5fae956f1b085370ec292bab5c7e25`, adding only the
[measured derivative](../../prototype/hierarchical-with-rstat.patch).

- `samples.tsv`: all 81 measured observations and 18 warmups; integer timings
  are nanoseconds. `sequence` preserves execution order within each boot.
- `summary.tsv`: four metrics, per-boot and pooled-baseline mean, median, sample
  standard deviation, CV, range, absolute/relative difference and return drift.
  Warmups are excluded; each compared boot has nine observations per case.
- `admissions.tsv`: 18 state/path checks, with no diagnostic timings promoted
  into clean samples. `NA` means not probed, not zero.
- `points.tsv`, `builds.tsv`: measured program, base commit and build identities.
  `baseline.config` and `prototype.config` are the measured full configurations,
  differing only in `CONFIG_LOCALVERSION`; no signing keys are included.
- `runtime.tsv`: matched runtime settings; root-device identity and build-host
  names are omitted. CPU/VM/security/preemption settings are retained.
- `boundaries.tsv`: 16 successful checks and one failed test premise, described
  in [the boundary notes](../../prototype/README.md). These are not timing samples
  or an exhaustive concurrency proof.

The original `zswap_case.c` was used unchanged. The binary SHA-256 is
`c144ea711fce5e3ebf08026b887ecf29e219bad2510271dac418cbcee570cc79`.
Worker CPUs, controller CPU, private cgroup limits, host reserve and batch order
match the [original workload contract](../README.md). Conditions are `finite8`,
`unlimited8`, `finite1`, rotated by `(boot_index + repetition) % 3` in each phase.
Each invocation starts empty, stores 8192 pages, verifies contents and frees them.

Public usage statistics can lag by one batched page: finite1 compressed-byte
readings were 2,457,492 on the baseline and 2,457,792 on the derivative. Direct
store counts were identical; these readings are not a compression-ratio gain.
`pending_slot_bytes` retains any delayed swap-slot accounting rather than claiming
immediate complete release. Restore includes verification and may hit swap cache.
Program CPU includes waited children and internal cleanup, not all background CPU
or external cgroup teardown.

See the [result interpretation](../../README.md) before using mean percentages.
Offline recomputation from the candidate root: `python3 reproducer/verify_results.py`.
`../counter-source-sha256.tsv` covers both evaluation datasets, configurations,
the measured patch and the added reproducer variant. Raw-output replay was checked
separately before compact export; offline verification is not a new kernel test.
