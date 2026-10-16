# Anonymous THP swap-fallback page ordering

A reclaim-local prototype reduces summed PAGEOUT plus cached-refault CPU
time by **14.84% / 13.14%**, saving **565.13 / 521.17 us per 8 MiB**, in
one-/two-worker synthetic tests on a fixed MM maintenance-tree pair.
Base-page controls show no clear change. This is an optimization candidate,
not a confirmed introducing-commit regression or an application-speed claim.

## Mechanism and scope

The workload begins with anonymous 2 MiB THPs and uses a file-backed swap
area. When swap allocation returns `-E2BIG`, reclaim splits a folio into
4 KiB pages. The split helper appends children in ascending physical order,
but reclaim takes them from the list tail. They therefore reach the write
path in descending order, after the original head.

The [tested prototype](prototype/reclaim-order.patch) changes only this
fallback in `mm/vmscan.c`: it reverses insertion of successfully split
children into the private reclaim queue. It does not reorder the original
head or existing entries, and does not change the common split helper,
migration callers, refcounts or lock/unlock operations. The added traversal
is included in the measured cost. The anonymous, non-swapcache, PMD-order
guard leaves other states on the original path.

This was discovered while exploring `mm/swapfile.c`; the proposed change
is in **`mm/vmscan.c`**, not swapfile's shared-reference-count code.

## Fixed pair and results

Source: [`akpm/mm-stable 2967f3ccdb5f`](https://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git/commit/?id=2967f3ccdb5fae956f1b085370ec292bab5c7e25),
Makefile base 7.3-rc5. Baseline → prototype → same baseline; GCC 15.2,
matching resolved configuration except the release suffix. Each point has
two warmups and nine measured invocations in each of four conditions:
**108 formal samples and 24 warmups**, no filtering or retries.

Primary metric: summed worker CPU time for PAGEOUT and cached refaults,
in microseconds per 8 MiB. Each invocation contains four cycles; these are
not counted as four independent samples. Two workers share the same total
8 MiB rather than doubling it.

| Condition | Baseline A | Prototype | Baseline B | Change vs combined baseline | Return drift |
| --- | ---: | ---: | ---: | ---: | ---: |
| Base pages, 1 worker | 3509.920 | 3524.923 | 3501.185 | +0.55% | −0.25% |
| Base pages, 2 workers | 4239.868 | 4218.953 | 4228.021 | −0.35% | −0.28% |
| Initial THP, 1 worker | 3804.692 | 3243.688 | 3812.945 | −14.84% | +0.22% |
| Initial THP, 2 workers | 3978.957 | 3445.816 | 3955.020 | −13.14% | −0.60% |

All prototype THP samples are faster than all corresponding baseline
samples; maximum within-point CV is 0.97% / 1.26%. The controls overlap.
[All samples, cycle records, configurations and calculation](bare-metal/README.md)
are included, not only favorable cases.

Platform: bare-metal Intel Core i7-12700KF, 32 GiB installed RAM, one NUMA
node; ext4 swap file on NVMe; translated IOMMU DMA mappings with lazy
invalidation. Worker CPUs 2/4 are separate physical cores. Full preemption,
performance governor on CPUs 2–5, turbo disabled, zswap disabled. Private
cgroups limit memory to 128 MiB and swap to 64 MiB; no global pressure.

## Validation and limitations

Twenty-four separate non-timing invocations checked entry, state, content,
cleanup and ordering. For serial THP, both kernels submitted 65 BIOs per
8 MiB. In the observed PAGEOUT caller, 8,384,512 bytes required 2,047
IOVA links on the baseline and 64 with the prototype. This is internal
mapping work, not a measured disk-I/O reduction; it excludes work outside
that caller. It supports the mechanism without assigning every saved
microsecond to one function.

The source review and 4,480 local list-model cases check output ownership,
order, errors and guards; five deliberately wrong variants are detected.
They do **not** establish actual kernel fault-injection or exhaustive race
coverage. Failure handling preserves any returned children and the error;
other callers stay unchanged. See [review boundaries](review.md).

The metric excludes initial filling/collapse, a fixed 20 ms wait, full-data
checking, and cleanup. The barrier including that wait and checking improves
by only 2.12% / 1.27%; it too is not an application lifecycle benchmark.
No real application benefit, cross-machine generality, or complete recovery
of the earlier cross-version difference is claimed.

[Exact benchmark source and standalone instructions](reproducer/README.md).
[Upstream snapshot and submission status](upstream-status/README.md).
