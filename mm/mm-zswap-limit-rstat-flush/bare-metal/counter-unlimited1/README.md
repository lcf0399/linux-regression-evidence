# Single-worker unlimited control

This is a separate `baseline → derivative → baseline` batch, not replacement
samples for the earlier noisy eight-worker control. It uses the identical kernel
pair and configurations in [counter-mm-pair](../counter-mm-pair/README.md).

All three boots use the new `zswap_case_unlimited1.c` variant: only case selection
and the selftest case count changed. [The exact source diff](../../reproducer/unlimited1-source.diff)
is included. The measured binary SHA-256 is
`f293b139ba4f804e21ee3d1f42af512f3dd3056728c6d95fc4a58030775e9fff`.

The case is `unlimited1`: one worker on CPU 0, controller on CPU 18, 32 MiB total,
`memory.zswap.max=max`, and the same private cgroup limits and cleanup contract.
Per boot: two independent state/path checks, two warmups and nine measured runs.
All six path/state admissions passed; successful stores are 8192 and limit-check
forced flushes are zero on both kernels.

`samples.tsv` retains all 27 measurements and six warmups; `summary.tsv` excludes
warmups and keeps the return drift. `admissions.tsv`, `points.tsv` and `runtime.tsv`
retain the separate batch identities. Timing units are nanoseconds.

PAGEOUT CPU averages +0.41%, program CPU +0.86%; these changes are small relative
to within-group variation and do not establish an extra cost. Nor do they prove
equivalence or settle concurrency/deep-tree/cold-decompression costs. All nine
observations are used; no sample was dropped to reach this conclusion.

Run `python3 reproducer/verify_results.py` from the candidate root to recompute
all three batches without executing a workload.
