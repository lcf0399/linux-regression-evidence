# RT sysctl reads: validation of Joseph Salisbury's rebuild guard

On v7.2, Joseph Salisbury's patch reduces repeated reads of
`sched_rt_period_us` and `sched_rt_runtime_us` from about 8.57 µs to
0.264 µs per read, a 96.9% reduction in this bare-metal microbenchmark.
The measured interval includes returned-value checks. This is validation of an
[existing patch](https://patchew.org/linux/20260313183716.990792-1-joseph.salisbury@oracle.com/),
not a new fix or a claim about application performance.

[中文](README.zh-CN.md)

## Results

2026-09-23, one original A → patched → original B matrix, with a fresh boot
at each point. Mean wall-clock time in ns/read, lower is better:

| Parameter | Original A | Patched | Original B | Reduction vs A/B midpoint |
| --- | ---: | ---: | ---: | ---: |
| `sched_rt_period_us` | 8547.294 | 264.575 | 8576.337 | 96.910% |
| `sched_rt_runtime_us` | 8574.240 | 263.690 | 8566.828 | 96.923% |
| `sched_rr_timeslice_ms` control | 253.400 | 250.268 | 251.304 | 0.826% |

The two RT cases save about 8.3 µs/read. Their maximum within-boot sample CV
is 0.891%; original A/B drift is 0.339% and 0.086%. Dropping the first measured
sample at each boot still gives reductions of 96.913% and 96.920%.
The RR control is below the predefined 5% effect threshold; it is not an
improvement claim. No outliers or control costs were subtracted.

Each boot uses one reader pinned to CPU 2. Each case has three warm-up and
nine measured samples, rotating the case order. A sample contains 4096
`pread(fd, buffer, ..., 0)` calls on a pre-opened FD, with an exact value and
length check after every read. Timing excludes opening/closing the FD, process
startup, environment setup and tracing. It is not the isolated syscall cost.
All 108 samples, including 27 warm-ups, are preserved in
[measurements.tsv](bare-metal/20260923-patch-pair/measurements.tsv).

## Build and runtime

- Intel Core i7-12700KF, all 20 logical CPUs online, one NUMA node and one LLC.
- Both builds use v7.2 (`8d3ae59288f1e7d58d76558a6ee96d533bc5019f`), GCC 15.2.0
  and GNU ld 2.46. The patch changes only `kernel/sched/rt.c`.
- The actual Kconfig symbols match except `CONFIG_LOCALVERSION`; the two
  original configuration files are included. Runtime preemption is `full`.
- Performance governor/EPP, Turbo disabled, `CONFIG_RT_GROUP_SCHED=n`.
- RT period/runtime are 1000000/950000 µs; RR timeslice is 100 ms on both kernels.
- The same reader binary is used at all points. Tracing is disabled for timing.

[Identity and protocol](bare-metal/20260923-patch-pair/identity.json) include
build hashes, relevant settings, thresholds and per-boot timestamps.
Configuration headings differ (`x86` versus `x86_64`), but they are comments,
not configuration changes. An earlier preparation attempt stopped on that
heading comparison before any boot or timing; the single completed matrix
above reused the same kernel build. Settings and the original boot state were
restored after testing.

## What the patch changes, and what was checked

The original proposal is Joseph's March 13 patch,
`[PATCH] sched/rt: Rebuild domains only after successful RT sysctl writes`.
The [tested patch](attribution/joseph-v7.2-context.patch) changes context only:
v7.2 no longer has the `sched_rt_do_global()` call. Its three logic changes are
unchanged: initialize a local rebuild flag to false, set it after a successful
write/update, and call `rebuild_sched_domains()` only when the flag is set.

Independent, untimed probes bracketed each individual read or write. They did
not include subsequent readback or restoration in the probe window:

| Operation | Original | Patched |
| --- | --- | --- |
| RT read | handler, rebuild, partition and deadline re-accounting each run once | handler runs once; the other three do not run |
| Successful write, including the same value | global update and rebuild both run | same |
| Tested rejected write | no global update, but rebuild runs | neither runs |
| RR read control | RR handler runs; no RT rebuild | same |

For all traced RT reads, `build_sched_domains()` ran **zero** times, including
on the original kernel: it reused existing domains. The patch avoids unnecessary
maintenance and deadline bandwidth re-accounting, not a full reconstruction on
every read. Scheduler-domain mutex lock/unlock pairs fell from two to one per
RT read; the initial locking remains.

Successful changes, same-value writes, invalid text, out-of-range values and
invalid period/runtime combinations had matching return values and readback.
Rejected writes preserved the old values. No kernel warnings or taint were
observed. [Read probes](bare-metal/20260923-patch-pair/read-probes.tsv) retain
all 72 operation records; [write checks](bare-metal/20260923-patch-pair/write-checks.tsv)
retain all 72 checks, with and without tracing. Function-entry evidence is not
exhaustive branch or concurrency coverage.

Not tested: concurrent writes, CPU hotplug, active SCHED_DEADLINE admission
failure, multiple NUMA nodes, `CONFIG_RT_GROUP_SCHED=y`, successful-write
performance, or end-to-end monitoring applications. These checks do not prove
complete patch safety. Release comparisons motivated this test, but this
bundle does not attribute the entire v6.12.95→v7.2 increase to one commit.

## Reproduce or inspect

- [Reader and safety notes](reproducer/README.md): standalone source, build and
  read-only commands; separate write-check source is also included.
- [Recompute the published numbers](bare-metal/20260923-patch-pair/recompute.py):
  `python3 bare-metal/20260923-patch-pair/recompute.py` from this directory.
  It uses only this bundle, does not run a benchmark, and compares its output
  against [summary.json](bare-metal/20260923-patch-pair/summary.json).
- [Dated upstream status](upstream-status/README.md): public proposal, inspected
  mainline revision, and the distinction between local validation and merging.
