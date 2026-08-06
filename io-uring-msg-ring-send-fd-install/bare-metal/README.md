# Bare-metal results

The primary result is an exact direct-parent comparison around
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
(`io_uring/rsrc: get rid of per-ring io_rsrc_node list`). Three independent
boots were run in this order:

```text
e410ffca5886 parent A -> 7029acd8a950 child -> e410ffca5886 parent B
```

Each point used 3 warm-up rounds followed by 15 measured rounds. Every round
performed 4,096 successful fixed-file installations in batches of 64. The
mean end-to-end cost in ns/install was:

| point | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 102.476 | 0.705% |
| child | 114.441 | 0.289% |
| parent B | 102.576 | 0.340% |

The child was `11.621%` slower than the midpoint of the two parent controls.
Dropping the first measured round from every point gave `11.649%`; parent
drift was `0.097%`. All 45 measured rows passed the CQE, slot-allocation,
sentinel, affinity, and unexpected-result checks.

The kernels used the same normalized config, GCC 15.2.0 toolchain, Kbuild
metadata, module-signing key, equal-length release strings, and identical
workload binary. Runtime preemption was `full` at all three points.
[`build-identity.tsv`](build-identity.tsv) and
[`exact-ab-points.tsv`](exact-ab-points.tsv) record the compact identities and
point statistics. [`measured-rounds.tsv`](measured-rounds.tsv) contains the 45
selected per-round timing rows needed to reproduce the mean, CV, drop-first
and semantic-pass checks without carrying the raw runner workspace.

## Supporting comparisons

Two independent matched sandwiches give the same direction:

- Linux 6.13 was `10.747%` slower than the Linux 6.12 control midpoint;
- Linux 7.1.3 was `15.602%` slower than the Linux 6.12.95 control midpoint.

These are release-level checks, not substitutes for the exact commit result.
All compared points had actual runtime `preempt=full`. The three comparisons
are collected in [`result-summary.tsv`](result-summary.tsv).

## Short-source cross-check

The 422-line F0-only source was also run across fresh exact-kernel boots in
the order parent A, child, parent B:

| point | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 101.239 | 0.440% |
| child | 114.010 | 1.776% |
| parent B | 103.917 | 0.441% |

The child was `11.145%` slower than the parent midpoint; drop-first gave
`11.147%`. All 45 rows passed the semantic checks and actual preemption was
`full` at every point. A separate untimed 256-operation smoke trace observed
256 calls each to `io_msg_ring()`, `io_msg_install_complete()`, and
`__io_fixed_fd_install()`.

Parent drift was `2.645%`, slightly above the formal `2%` gate. This result is
kept as a compact directional cross-check and is not promoted as a second
formal result. See [`standalone-cross-check.tsv`](standalone-cross-check.tsv).

## Slot-count scope check

The exact-kernel parent A, child, parent B sequence was repeated with 64, 256,
1,024, and 4,096 target slots, while keeping the queue depth at 64. The child
was respectively `11.853%`, `8.717%`, `8.399%`, and `11.889%` slower than the
matched parent midpoint. All 180 measured rows passed the semantic checks.

The 64-slot child and 256-slot parent controls were noisier than the larger
points, so this is a scope check rather than a replacement for the primary
4,096-slot result. It shows that the direction is already present at one
64-operation batch and is not confined to a very large fixed-file table. The
effect does not grow monotonically with the table size. See
[`slot-gradient.tsv`](slot-gradient.tsv).

## Untimed mechanism trace

An untimed parent/child trace used 128 successful SEND_FD installations on
each kernel. Both points observed 128 calls to `io_msg_ring()`,
`io_msg_install_complete()`, and `__io_fixed_fd_install()`. Calls to
`io_rsrc_node_alloc()` nested under fixed-file installation changed from 0 in
the parent to 128 in the child; total calls changed from 4 to 129.

This confirms that the workload directly reaches the new per-install resource
node path. It does not prove that one allocator function accounts for the full
timing difference. See [`mechanism-summary.tsv`](mechanism-summary.tsv).

## Platform and scope

The physical machine was an Intel Core i7-12700KF system with 32 GiB RAM. The
single process was pinned to P-core logical CPU 2, the scaling governor and
EPP were `performance`, and Turbo was disabled.

This evidence concerns fixed-file registration/update through
`IORING_MSG_SEND_FD`. It is not a claim about ordinary io_uring read/write
submission, application performance, or every fixed-file update pattern.
