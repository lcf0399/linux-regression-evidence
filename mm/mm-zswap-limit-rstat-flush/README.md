# Per-page statistics flush cost under a finite zswap limit

[中文说明](README.zh-CN.md)

## Upstream status — November 11, 2026

Supplementary results were sent to the original discussion thread. Gmail Sent
independently confirmed the body, reply headers and metadata of the reproducer
source and measured-patch attachments. Public archival is not confirmed; sending
does not establish acceptance or merge. See the [short status record](upstream-status/README.md).
The main result is single-worker 32 MiB; the current concurrency supplement is
eight workers totaling 256 MiB. The older 32 MiB concurrency batches below keep
their original identities and are not pooled with it.

This bundle adds exact-commit bare-metal evidence to the already discussed cost of
zswap limit checks, followed by an evaluation of a derivative of Joanne Koong's
hierarchical per-CPU counter experiment. The derivative retains existing rstat
updates. It shows a finite-limit benefit; a separate single-worker unlimited
control detects no clear added cost. The separate 256 MiB concurrency supplement
supports the finite-limit gain, with drift limiting effect-size precision.
The batches answer different questions and are not pooled into a universal
fix or application-speedup claim.

The workload prepares 32 MiB of compressible private anonymous memory, disables
THP, sets `memory.zswap.max=64 MiB`, and calls `MADV_PAGEOUT`. All 8192 base pages
are stored successfully without reaching the limit or inducing OOM. It then
accesses and verifies every data word and releases the mappings. The historical
eight-worker variants keep the same total work; the November 10 supplement uses
32 MiB per worker, 256 MiB total. An unlimited variant is a path control, not
a proposal to remove limits. Per-cgroup limits can bound shares of the common
zswap pool, but application frequency and production impact remain unmeasured.

## 1. Exact-commit attribution: parent / child / parent

- Parent: `b0543d50c4cbc34ca612ab17c3cc9f2d174be8e0`.
- Child: [`0efc297a3c4974dbd609ee36fc6345720b6ca735`](https://git.kernel.org/torvalds/c/0efc297a3c4974dbd609ee36fc6345720b6ca735),
  `cgroup/rstat: avoid disabling irqs for O(num_cpu)`.
- Complete trees differ only in the commit's `kernel/cgroup/rstat.c` change.
  Configurations differ only in the release suffix; both use GCC 15.2.0 / GNU C11.
- Core i7-12700KF, 20 online logical CPUs, LZO / zsmalloc, actual full preemption.
- Three boots, two warmups and nine measured repetitions per condition per boot:
  18 separate admission checks, 81 clean samples and 18 warmups.

| Metric, ms / 32 MiB | Parent A | Child | Parent B | Versus pooled parents |
| --- | ---: | ---: | ---: | ---: |
| One worker, finite limit, PAGEOUT CPU | 17.180 | 24.464 | 17.316 | +7.216 ms / 41.84% |
| Same condition, complete program CPU including waited children | 46.215 | 53.419 | 46.235 | +7.193 ms / 15.56% |
| Eight workers, finite limit, summed PAGEOUT CPU | 283.082 | 298.696 | 288.385 | +4.54%; noisy, not a stable magnitude |
| Eight workers, unlimited, summed PAGEOUT CPU | 43.438 | 43.890 | 43.520 | +0.95%; noisy, no clear difference |

For the single-worker primary metric, return drift is 0.80%; per-boot CVs are
2.82%, 2.13% and 2.07%. Parent samples span 16.663–17.771 ms; child samples span
23.506–24.922 ms. Multi-worker CVs are about 24–43%, so their mean differences
do not establish stable effects.

## Mechanism and limits

Separate probes observed 8192 successful stores in each condition. Finite-limit
cases performed 8192 forced limit-check flushes during PAGEOUT; unlimited cases
performed none. The commit releases and reacquires the lock after each possible
CPU to bound IRQ-off latency. An earlier, separate release-version diagnostic
observed lock operations rising from one to 21 groups per flush; that diagnostic
is not a per-lock timing result and is not pooled with these clean samples.

The original IRQ-latency protection remains necessary. The historical parent is
an attribution control, not a recommended fix. These results establish a scoped
commit contribution, not a complete explanation of all release-version changes.
The following counter evaluation is a separate experiment on a different base.

PAGEOUT CPU is summed worker thread CPU time, not throughput. The program CPU
metric includes preparation, verification and internal cleanup, including waited
children, but excludes external cgroup setup/teardown and system-wide background
CPU. Restore may hit swap cache; this is not a cold-load/decompression benchmark.
Content and final compressed-page release were checked. Deferred logical swap
slot charges are retained separately, not described as immediate full release.

## 2. Counter evaluation on a matched MM base

The base is `2967f3ccdb5fae956f1b085370ec292bab5c7e25`; the other kernel adds only
the [measured patch](prototype/hierarchical-with-rstat.patch). Configurations differ
only in the release suffix; toolchain and runtime settings match. This adapts the
[public experiment](https://github.com/joannekoong/linux/commit/9092e0057f038ffca43ae3f35faf58c33b1d7c9b),
not a new accounting algorithm or a verbatim retest of the author's patch.

Instead of flushing the statistics tree before each finite-limit store, the
derivative reads a dedicated counter summed across CPUs. Store/free operations
update ancestor counters and retain existing rstat updates for public statistics
and reclaim consumers. The measured cost includes both updates. It neither
removes the IRQ-latency protection nor substitutes a periodic stale-stat check.

Separate path checks observed 8192 successful stores on both kernels, with
limit-check forced flushes falling from 8192 to zero. The probe-free
baseline A / derivative / baseline B series has 18 admission checks, 81 measured
observations and 18 warmups.

| Metric, ms / 32 MiB | Baseline A | Derivative | Baseline B | Versus pooled baselines |
| --- | ---: | ---: | ---: | ---: |
| One worker, finite, PAGEOUT CPU | 23.903 | 13.113 | 23.514 | −10.595 ms / 44.69% |
| Same condition, complete program CPU | 49.247 | 38.569 | 49.363 | −10.737 ms / 21.78% |
| Same condition, restore and verification CPU | 11.742 | 11.802 | 11.896 | −0.15%; no clear change |
| Eight workers, finite, summed PAGEOUT CPU | 255.966 | 43.199 | 205.510 | Clear direction, unstable magnitude |
| Eight workers, unlimited, summed PAGEOUT CPU | 37.150 | 42.024 | 29.211 | +26.65%; noisy, unresolved |

Single-worker finite PAGEOUT return drift is −1.63%; CVs are 1.88%, 3.55% and
2.91%. Derivative samples span 12.445–13.769 ms versus 22.745–24.658 ms for the
baselines. The complete-program reduction is not merely a transfer between the
measured phases.

All eight-worker finite PAGEOUT samples improve, but return drift is −19.71%
and the derivative CV is 58.92%; the descriptive −81.28% mean is not a stable
effect size. Restore/verification CPU averages +61.60%, also with large variation;
complete-program CPU averages −59.52%. Eight-worker unlimited program CPU averages
+22.96%, also noisy. These unfavorable or unstable results are retained in the
[batch data](bare-metal/counter-mm-pair/README.md), not omitted from the assessment.

## 3. Separate single-worker unlimited control

The same kernel pair was tested with an added `unlimited1` selector, without
rerunning the noisy group. Timed worker, data and cleanup logic are unchanged.
This batch has six admission checks, 27 measured observations and six warmups.

| Metric, ms / 32 MiB | Baseline A | Derivative | Baseline B | Versus pooled baselines |
| --- | ---: | ---: | ---: | ---: |
| PAGEOUT CPU | 12.225 | 12.208 | 12.093 | +0.049 ms / 0.41% |
| Complete program CPU | 37.503 | 37.802 | 37.456 | +0.323 ms / 0.86% |
| Restore and verification CPU | 11.594 | 11.534 | 11.589 | −0.057 ms / 0.50% |

PAGEOUT return drift is −1.08%; per-boot CVs are 4.72%, 4.74% and 4.47%.
Program CPU CVs are 1.97%, 3.48% and 1.79%. No clear added cost was detected,
but this is not equivalence testing or evidence about eight-worker, deep-tree or
production-scale costs. See the [separate control data](bare-metal/counter-unlimited1/README.md).

## 4. November 6 eight-worker repeat: direction repeats, variability remains

The same kernel pair, eight P-cores and 32 MiB total were retained. Only the
pre-timing statistics gate changed to bounded convergence waiting; it does
not force a statistics flush or change the timed worker. All 54 measured
calls, 12 warmups, 18 admission checks and recovery passed independent review.

| Condition / metric, ms per 32 MiB | Baseline A | Derivative | Baseline B | Descriptive mean change |
| --- | ---: | ---: | ---: | ---: |
| Finite, PAGEOUT CPU | 237.461 | 47.073 | 220.690 | -79.45% |
| Finite, Restore/verification CPU | 19.360 | 31.657 | 19.796 | +61.70% |
| Finite, Complete-program CPU | 288.640 | 118.682 | 272.558 | -57.70% |
| Finite, Complete-program wall | 46.525 | 26.988 | 44.829 | -40.92% |
| Unlimited, PAGEOUT CPU | 30.483 | 40.768 | 28.447 | +38.36% |
| Unlimited, Restore/verification CPU | 25.163 | 30.690 | 21.751 | +30.84% |
| Unlimited, Complete-program CPU | 91.151 | 111.809 | 83.321 | +28.17% |
| Unlimited, Complete-program wall | 22.706 | 26.068 | 21.604 | +17.66% |

These are not stable effect sizes. Finite PAGEOUT CVs are 37.16%/57.62%/40.91%,
with −7.06% return drift; unlimited CVs are 32.93%/63.90%/27.43%, with −6.68% drift.
All nine finite derivative samples (24.750–101.388ms) remain below all 18
baseline samples (164.978–398.153ms). Restore and unlimited distributions
overlap: retain the unfavorable means without calling them stable penalties
or asserting no concurrency cost.
[All 54 observations and recomputation](bare-metal/counter-eight-repeat/README.md)
are separate from the original three batches.

## 5. November 10 eight-worker supplement: 32 MiB per worker, 256 MiB total

On the same MM pair, eight distinct P-cores process 65,536 pages in one shared
cgroup. `memory.max=2 GiB`, `memory.swap.max=1 GiB` and finite
`memory.zswap.max=64 MiB`; measured compressed occupancy is 18.7518–18.7520 MiB,
below the quota. No high/max/OOM events occurred. The new batch independently
passed 54 measured calls, 12 warmups and 12 non-timing state/path admissions,
with two warmups and nine samples per condition at each A/B/A endpoint.

| Condition / metric, ms per 256 MiB | Baseline A | Derivative | Baseline B | Descriptive mean change |
| --- | ---: | ---: | ---: | ---: |
| Finite, PAGEOUT CPU | 1217.353 | 199.238 | 1279.380 | −84.04% |
| Finite, reaccess/verification CPU | 217.401 | 215.365 | 219.263 | −1.36% |
| Finite, complete-program CPU | 1686.122 | 663.404 | 1750.095 | −61.39% |
| Finite, complete-program wall | 221.937 | 94.618 | 231.435 | −58.26% |
| Unlimited, PAGEOUT CPU | 193.071 | 196.272 | 194.952 | +1.17% |
| Unlimited, reaccess/verification CPU | 214.656 | 214.950 | 214.851 | +0.09% |
| Unlimited, complete-program CPU | 656.704 | 660.535 | 661.254 | +0.24% |
| Unlimited, complete-program wall | 94.452 | 95.183 | 94.054 | +0.99% |

Derivative PAGEOUT CVs are 4.51% finite and 4.91% unlimited. Finite baseline
return drift is +5.10%, limiting precision of the −84.04% descriptive mean;
all nine derivative samples are 186.4–215.1 ms, below all baseline samples at
1206.5–1284.3 ms, so the benefit direction remains clear. Unlimited drift is
+0.97%; its +1.17% PAGEOUT mean is not a stable regression or an equivalence result.
The large adverse restore/unlimited means in the old 32 MiB batch did not repeat,
but workload size, private resource ceilings and date differ: neither pool the
batches nor attribute the old noise solely to workload size.

Separate path checks observed 65,536 successful stores, finite limit-check
forced flushes falling from 65,536 to zero, and zero unlimited flushes on both
kernels. The compressed quota is not reached; low compressibility, near-limit,
deep-tree and freely scheduled conditions remain outside this result. Reaccess
may hit swap cache, not pure cold decompression. [All 54 compact observations,
12 admissions and offline recomputation](bare-metal/counter-eight-scale256/README.md)
are exported separately; the original raw evidence and identities are retained.

## Correctness evidence and remaining limits

Sixteen successful bounded checks across the two kernels cover leaf/ancestor
zero and near limits, dynamic limits, cross-CPU free/reuse, two-CPU handoffs, and
live objects reparented after actual cgroup offlining before uncharge. Data and
final compressed-object cleanup checks passed.

One earlier dynamic-limit check failed because it incorrectly assumed that a
rejected pre-check could not reclaim old entries. The baseline can reclaim then
store; revised checks observe both return paths, with matching derivative behavior.
The failed premise is retained, not counted as a successful kernel check.
[Patch and boundary notes](prototype/README.md) explain this distinction.

This does not exhaust RCU races, CPU hotplug, 32-bit builds or deep/large trees.
Restore may hit cache and does not independently establish cold-decompression
performance. The counter sum is not an atomic cross-CPU snapshot; it does not
provide a strict instantaneous limit that the existing pre-check never promised.

## Value and discussion scope

The [existing discussion](https://lore-kernel.gnuweeb.org/cgroups/CAO9r8zNxnVYpwrsuB1Ofcwf-Lks2e%2BDL1Ys9CXZ5jozBug_ybQ@mail.gmail.com/T/)
considers less frequent flushes and dedicated counters. Stale statistics can
incorrectly reject stores; replacement accounting can add update-path costs.
All tables above are measurements from this research, not imported results from
that discussion. The contribution is scoped attribution plus evidence for a
derivative that retains the old statistics. Plausible usage, a per-page mechanism,
absolute cost and complete-program effects support reporting it as supplementary
evidence. Production frequency, application impact and larger-scale update costs
remain unknown. The useful discussion concerns the cost of retaining both counters
and which hierarchy, concurrency or fault-path conditions merit validation, not
whether this small matrix establishes a universally safe or optimal replacement.

See [reproduction](reproducer/README.md), [separate datasets](bare-metal/README.md)
and [the measured derivative and boundary notes](prototype/README.md).
Run `python3 reproducer/verify_results.py` for offline recomputation only.
