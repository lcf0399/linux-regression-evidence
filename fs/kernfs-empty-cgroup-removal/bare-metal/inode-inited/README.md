# INODE_INITED patch validation

2026-09-14. T.J. Mercier's change reduced empty-cgroup removal latency by
21.87–22.86% on the original binary. The 128-operation stage follow-up
confirmed faster removal, but continuous whole-sequence timing remained noisy.
A separate [32-operation pacing diagnostic](pacing/README.md) found a stable
5.77–8.76% whole-sequence reduction with 16 warmups and continuous operation.
It retains all six conditions and does not replace the earlier noisy result.
These are patch-validation results, not proof of complete concurrency safety
or a claim that the original regression is fully fixed.

## Exact comparison

Both states start at `2f0c1cf72f4682178506f513bbf015e591b1aa4a` (7.3-rc2):

- A: base + Shakeel Butt's file-handle decoding read-lock fix.
- B: A + T.J. Mercier's INODE_INITED changes, with no changes to their logic.

The [read-lock fix](https://lists.openwall.net/linux-kernel/2026/09/05/828)
is a prerequisite on this base. T.J.'s exact original base is unknown;
no T.J.-only kernel was tested. The supplied [patches](patches/) reconstruct
the actual compiled source differences, with source and original patch hashes
in [identity.json](identity.json) and [provenance.json](provenance.json).

Bare-metal i7-12700KF; caller on CPU0; sched_ext disabled; full preemption;
performance governor; Turbo disabled; GCC 15.2. The configs differ only in
LOCALVERSION. Each experiment used fresh boots in order A1 → B1 → B2 → A2,
with nine samples per condition per boot, 16 warmups and 128 measured
operations per sample. Both matrices completed and restored generic.
The original and follow-up binaries differ; their samples are never pooled.
The additional 32-operation diagnostic has its own [identity and results](pacing/README.md).

## Original binary: deletion improvement

Values are medians of nine 128-operation sample means, in microseconds.

| Metric | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| rmdir | 9.727 | 7.600 | 7.548 | 9.784 |
| CV, % | 0.419 | 0.477 | 2.027 | 0.498 |

All four B/A comparisons improved by 21.87–22.86%. Repeat-boot drift was
+0.588% for A and −0.680% for B. Dropping only the first sample as a
sensitivity check preserved the result. All measured tails remain in the data.
Creation was noisy (CV 4.02–8.31%; drift −4.65% / −2.55%).

Separate untimed traces show 26 kernfs nodes per removal in both states,
with `ilookup()` calls reduced from 26 to 1. Deletion write/read-lock counts
remain 52/26. Each state passed 16 no-watch deletion cycles, eight
file/directory notification checks, and eight held-open-FD link-count checks.
These are basic checks, not exhaustive concurrent inode-eviction tests.

## Separate stage-timing follow-up

The new binary times first `stat()` and the elapsed sequence from `mkdir()`
start through `rmdir()` return. The sequence is measured by an enclosing
clock, not by adding stage medians. It includes userspace transitions and
clock reads. It excludes pacing pauses and the post-removal state check.
The separate loop-wall metric includes those pauses/checks; neither metric
is total CPU cost including all deferred reclamation.

| Continuous, µs | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| mkdir | 18.858 | 19.123 | 19.072 | 18.439 |
| first stat | 1.787 | 1.803 | 1.790 | 1.796 |
| rmdir | 9.714 | 7.541 | 7.556 | 9.709 |
| full sequence | 30.438 | 28.495 | 28.453 | 30.042 |

Deletion improved by 22.17–22.37% with stable timing. First stat had a
maximum CV of 3.76%, so the added lock's cost cannot be precisely quantified.
Full-sequence times were 5.15–6.52% lower, but CV was 4.52–6.46%.
Creation CV was 7.18–8.71%. These overall measurements suggest a benefit,
but do not establish a stable overall improvement percentage.

Untimed traces locate the added write lock in first stat's inode
initialization: zero → one lock/unlock pair. The patch skips lookups for
nodes whose inodes were never initialized; it does not move those lookups
into stat. The same 26 → 1 deletion lookup counts held.

### Pacing diagnostic

The same follow-up matrix also requested a 50 ms pause outside every
measured sequence, alternating condition order between samples. The pause
is already excluded from the numbers below; do not subtract it again.

| 50 ms pacing, µs | A1 | B1 | B2 | A2 |
| --- | ---: | ---: | ---: | ---: |
| mkdir | 54.094 | 55.463 | 54.567 | 54.553 |
| first stat | 8.141 | 8.149 | 7.899 | 7.895 |
| rmdir | 18.110 | 15.879 | 15.811 | 18.253 |
| full sequence, pause excluded | 80.595 | 79.479 | 78.531 | 80.728 |

Deletion improved by 12.32–13.38%. Full-sequence time was 1.38–2.72% lower;
CV fell to 0.59–1.23%, with A/B drift +0.165% / −1.193%. This passed the
stability checks, but stayed below the preset 5% promotion threshold.
That does not mean zero benefit. Paced stat still had drift −3.021% / −3.071%,
despite CV below 3%; not all stages were stable.

Pacing changes execution state and absolute latency. This diagnostic does
not prove that asynchronous cleanup caused the continuous noise, or that
50 ms guarantees cleanup completion. It is not an independent second matrix.

## Reproduce and verify

The [original reproducer](../../reproducer/README.md) remains unchanged.
The [follow-up source](reproducer/control.c) is byte-identical to its tested
build input. Both link libbpf but load no BPF program in disabled mode.
From `reproducer/`, build with `make`. On a dedicated matching host:

```sh
sudo env -u CG_GRAPH -u CG_TRACE CG_SCX_DISABLED=1 CG_CYCLE_PACE_MS=0 \
  ./build/control unused leaf 0 0 0 0 128 16
```

Set `CG_CYCLE_PACE_MS=50` for the separate pacing diagnostic. These commands
create a private cgroup subtree; do not run them on a shared production host.
Record a new binary hash after rebuilding; do not assume historical identity.

[Ordered samples](measured-rounds.tsv) contain 36 original and 72 follow-up
invocations (504 metric rows, not 504 independent samples).
[Summary](summary.json) retains every stage, noise and loop-wall result.
[Mechanism](mechanism.json) is separate non-timing evidence. Public verification:

```sh
python3 -B verify.py
```

This checks artifact hashes and independently recomputes medians, population
CVs, repeat-boot drift and all four endpoint/drop-first comparisons.
The export also replayed the full local raw timing and trace records.
Private mail, credentials and raw trace files are not included.
