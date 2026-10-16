# Validation of `1e7cd8a53b72` Per-superblock Cache Fix

Date: 2026-07-16 UTC

Status: fix validated on bare metal for the scoped microbenchmark

## Question

The original report measured a small-list `flistxattr(fd)` slowdown on tmpfs.
Jan Kara asked whether it was still observable after:

```text
1e7cd8a53b72 simpe_xattr: use per-sb cache
```

The subject spelling above is the original upstream commit subject.

The released-kernel point in the original report was Linux 7.1.0
(`7.1.0-bm-7.1`).  This follow-up also tested Linux 7.1.3 as an updated stable
7.1 point.  Neither 7.1.0 nor 7.1.3 contains `1e7cd8a53b72`; the patched point
is the exact child commit below.

## Source Coordinates

```text
parent  076e5cef28e27febfc09b5f72544d2b857c75201
child   1e7cd8a53b72a58a44c4d282aed95f6ce0e76db0
```

These are a direct parent/child pair.  The commit later entered Linus' tree
through merge commit
[`ff8747aacaff`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux.git/+/ff8747aacaff8266dd751b8a8648fb728dcc3b21)
during the v7.2 merge window; it was not present in the tested Linux 7.1
stable points.

## Method

Each point used a fresh boot in this order:

```text
7.0.14 A -> 7.1.3 -> parent A -> child -> parent B -> 7.0.14 B
```

The workload created a tmpfs file with one `user.*` xattr and repeatedly
called `flistxattr(fd, buffer, 8192)`.  Each point had 3 warm-up rounds and 15
measured rounds of 1,048,576 calls.  Every measured row required the expected
returned-list length and zero unexpected results.

The test ran on an Intel Core i7-12700KF system with 32 GiB RAM, pinned to
P-core CPU 2.  The scaling governor and energy-performance preference were
both `performance`, Turbo was disabled, and the kernel command line selected
`preempt=none`.  The parent and child used the same normalized configuration,
GCC 15.2.0 toolchain, and Kbuild metadata.

## Results

The primary metric is mean ns/op; lower is better.

| point | mean ns/op | CV |
| --- | ---: | ---: |
| Linux 7.0.14 A | 218.201800 | 0.518633% |
| Linux 7.1.3 | 336.140000 | 3.267516% |
| direct parent A | 327.037200 | 2.240271% |
| `1e7cd8a53b72` child | 208.446667 | 0.189207% |
| direct parent B | 323.640467 | 1.900889% |
| Linux 7.0.14 B | 217.007333 | 0.230280% |

Using the surrounding control midpoints:

| comparison | delta |
| --- | ---: |
| Linux 7.1.3 vs Linux 7.0.14 | +54.472861% |
| direct parent vs Linux 7.0.14 | +49.509194% |
| child vs direct parent | -35.929362% |
| child vs Linux 7.0.14 | -4.208505% |

The two Linux 7.0.14 controls drifted by `-0.547414%`; the two direct-parent
controls drifted by `-1.038638%`.  All 90 measured rows passed the semantic
checks.  Dropping the first measured round from every point produced the same
direction and nearly identical deltas.

## Interpretation

For this specific tmpfs one-xattr `flistxattr(fd)` syscall microbenchmark,
`1e7cd8a53b72` fully removes the reported slowdown and slightly exceeds the
Linux 7.0.14 baseline.  There is therefore no residual post-fix gap to profile
for this workload.

This result does not claim application-level impact or generalize to other
filesystems, xattr operations, or xattr-list shapes.
