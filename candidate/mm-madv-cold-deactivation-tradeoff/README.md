# MADV_COLD: a state-dependent MGLRU deactivation tradeoff

[中文说明](README.zh-CN.md) · [Reproduction](reproducer/README.md)

Exact parent/child/parent measurements of `cc8ec7be78ff` show both sides of its
deactivation optimization. Freshly populated pages pay for a new check without
skipping the subsequent list work. File pages already in the oldest generation
avoid that work and improve substantially. This is evidence about a state-dependent
tradeoff, not a recommendation to revert the optimization.

`MADV_COLD` asks the kernel to treat retained contents as less recently used.
It is not `MADV_DONTNEED`, which can discard the contents. Here, moving a page means
changing its reclaim-list membership, not copying its data. An “oldest generation”
is a reclaim classification, not a fixed elapsed age.

## Matched attribution

Parent: `9cbfd1c3c83b62c42ff46e17227e996cc9b2b336`.
Child: [`cc8ec7be78ffaa29f6e0781fd12602974970eb53`](https://git.kernel.org/torvalds/c/cc8ec7be78ffaa29f6e0781fd12602974970eb53),
`mm/mglru: optimize deactivation`.

The machine is a Core i7-12700KF, one NUMA node, 20 online logical CPUs; the
worker runs on CPU2 with 4 KiB base pages, MGLRU=0x7 and actual full preemption.
Both builds use GCC 15.2.0 / GNU C11 and matching configurations except for the
release suffix. Each series has parent A / child / parent B boots, nine measured
repetitions per condition per boot, and separate admission checks without using
their timings. The anonymous series contains 54 measured programs; the file
series contains 27 programs with two measured target phases each.

CPU time per 16 MiB operation; the parent column pools 18 samples and the child
column has nine:

| State and interface | Parent | Child | Difference | Change | Parent return drift |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fresh anonymous, one self `process_madvise` vector | 391.856 µs | 440.546 µs | +48.690 µs | +12.43% | +0.81% |
| Fresh anonymous, 64 self vectors | 410.961 µs | 460.531 µs | +49.571 µs | +12.06% | +0.10% |
| Fresh file, ordinary `madvise` | 387.297 µs | 438.417 µs | +51.120 µs | +13.20% | −0.27% |
| Oldest-generation file, ordinary `madvise` | 362.306 µs | 114.842 µs | −247.464 µs | −68.30% | −0.17% |

These are separate protocols, not interchangeable samples of one general effect.
For the file protocol, an additional one-page COLD call drains the final batch.
Including it gives +13.18% / −68.22%, rather than treating deferred parent work as
free. File operation CPU CVs are 0.36–0.90%; anonymous operation CPU CVs are
0.16–0.92%. Raw distributions and all measured boundaries remain available.

## State, mechanism and limits

The file test prepares a 16 MiB target and a 16 MiB auxiliary file in a private
cgroup capped at 192 MiB with swap disabled. One ordinary 4 KiB `memory.reclaim`
request prepares the oldest-generation state; the request size is not a claim
that only 4 KiB was actually reclaimed. The target remains fully mapped and
resident. A COLD call alone was not sufficient to establish this state.

Separate checks on each boot directly observed the target generation and work:
fresh file pages had 4096 enqueue/move callbacks on both kernels; oldest-generation
pages had 4096 on the parent and zero on the child. No probe remains during formal
timing. We do not claim a direct generation observation for every clean sample or
an instruction-by-instruction cost decomposition.

Complete-program CPU increases by only 0.57% / 0.47% in the anonymous protocols.
The file worker's descriptive −4.53% CPU change has −8.05% parent return drift;
it does not establish a stable whole-program improvement. Preparation, validation
and internal cleanup are included in the respective whole-program boundaries,
but external cgroup setup/teardown and system-wide background CPU are not.
The operation metric is neither storage throughput nor cold disk-read latency.

The [author's original report](https://lkml.iu.edu/2412.3/03612.html) addresses
high-concurrency buffered-I/O lock contention. It also discusses a possible
within-generation reclaim-order change, not the fresh-page COLD CPU cost measured
here. We did not reproduce its 400-job FIO result. The historical parent is an
attribution control, not a proposed fix; the result does not explain all later
release-version differences.

## Value and discussion

Retaining data while marking it cold is a legal, meaningful interface use.
This synthetic case establishes a per-page tradeoff and about 49–51 µs of added
CPU per 16 MiB in the fresh state. Application frequency, aggregate impact and
latency-budget relevance remain unmeasured.

The useful question is whether the fresh-state check can be made cheaper while
retaining the oldest-state benefit and required concurrency protection.
[MGLRU-FG v4](https://lkml.iu.edu/2610.1/19350.html) includes a related zero-reference
atomic-write avoidance, but also changes accounting and lifetime handling; this
bundle does not test that series or present it as a validated one-line fix.
Source checks on 2026-10-20 found the clear-references/oldest-generation helper
still present at
[mainline `f47d223627f8`](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/mm/folio.c?id=f47d223627f80cb7a1eb14c585fe6cb35de5e984),
[mm-stable `2967f3ccdb5f`](https://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git/tree/mm/folio.c?id=2967f3ccdb5fae956f1b085370ec292bab5c7e25)
and [mm-unstable `3eb3e848d72c`](https://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git/tree/mm/folio.c?id=3eb3e848d72cde2156447cc960ca321bb2ac41a9).
Source inspection is not a new-tree performance comparison.

The bundle includes unchanged measured programs, both configurations, 81 formal
program outputs and offline arithmetic verification. The configs replace only
the private signing-key filename with a standard relative path; original config
hashes are retained and no key is included. See [configuration notes](config/README.md).
Run
`python3 reproducer/verify_results.py`; this does not run a kernel experiment.
