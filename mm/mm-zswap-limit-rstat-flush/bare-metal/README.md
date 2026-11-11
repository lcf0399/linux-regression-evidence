# Separate measurement batches — original October 18 evidence and follow-ups

| Batch | Comparison | Measured / warmup / admission counts |
| --- | --- | ---: |
| Files directly in this directory | Exact historical parent / child / parent | 81 / 18 / 18 |
| [counter-mm-pair](counter-mm-pair/README.md) | MM baseline / derivative / baseline; three cases | 81 / 18 / 18 |
| [counter-unlimited1](counter-unlimited1/README.md) | Same MM pair; separate single-worker unlimited control | 27 / 6 / 6 |

Each batch has its own return point and fixed sample counts. Never combine the
historical parent with the later prototype, subtract their percentages, or use
the single-worker control to replace the noisy eight-worker observations.
The additional 16 bounded correctness checks and one failed test premise are
recorded separately from timing and per-boot admissions.

## Exact-commit dataset (original files retained)

- `samples.tsv`: all 81 clean measured observations and 18 warmups. Every timing
  column is in nanoseconds. All cases process exactly 32 MiB; do not divide the
  eight-worker total by eight and compare it to the one-worker total.
- `summary.tsv`: per-boot and pooled-parent statistics for four timing metrics;
  sample standard deviation, CV, min/max, mean differences and return drift.
  Warmups are excluded. Parent A and B have equal sample counts.
- `admissions.tsv`: 18 separate state/path checks. `NA` means no probe was active,
  not a zero count. No admission timing is included in `samples.tsv`.
- `builds.tsv`, `parent.config`, `child.config`: exact source/build identities and
  full configurations with the local signing-key path normalized to
  `certs/signing_key.pem`; no signing key is included. `builds.tsv` retains the
  original configuration hashes; `source-sha256.tsv` hashes the public copies.
  The tested configurations differ only in the local release suffix.
- `runtime.tsv`: matched power, VM, zswap, preemption, mitigations and CPU settings.
- `source-sha256.tsv`: original reproducer and configuration checksums.

Single worker uses CPU 0; eight workers use CPUs 0,2,4,6,8,10,12,14. The controller
uses CPU 18. Each boot rotates the three cases by `(boot_index + repetition) % 3`,
starting from `finite8, unlimited8, finite1`, separately for warmup and measurement.
The binary was identical across boots (SHA-256
`c144ea711fce5e3ebf08026b887ecf29e219bad2510271dac418cbcee570cc79`).

The private cgroup has memory/swap/pid limits of 256 MiB / 128 MiB / 32. The finite
leaf zswap limit is 64 MiB; ancestors are unlimited. Tests start with no active
zswap contents, an unused 8 GiB swap file and at least 4 GiB available RAM.
No global memory pressure is used. Runtime RT settings are 950000 / 1000000 us.
The timing controller restored temporary settings and the generic kernel after
each boot point; semantic, cleanup and recovery checks passed.

`pending_slot_bytes` is delayed logical swap accounting, allowed up to 5 MiB on
this 20-CPU machine. `batched_zswapped_bytes` and `batched_zswap_left` are batched
statistics, not direct page counts. `stored_left=0` establishes compressed-page
release; it does not establish immediate return of every swap slot charge.

Recompute with `python3 ../reproducer/verify_results.py`. The compact export is
checked against the original independently replayed raw-output summary. It does
not include SSH details, hostnames, private mail or raw exploratory traces.

## November 6 eight-worker follow-up

The [separate repeat](counter-eight-repeat/README.md) exports all 54 new measured observations,
its full-program CPU/wall metrics and preparation-only source variant. It does not
replace or augment the original three batches above. The verifier also checks this
fourth dataset; additional warmups, admissions and raw evidence remain local.

## November 10 eight-worker 256 MiB supplement

The [fifth separate dataset](counter-eight-scale256/README.md) processes 32 MiB per
worker, 256 MiB total, retaining the same MM builds, eight P-cores and 64 MiB finite
compressed quota. It exports 54 measured observations, 12 state/path admissions,
the source and compact identity. The private memory/swap ceilings scale to
2 GiB / 1 GiB; measured finite compressed occupancy stays below the quota.
It neither replaces nor augments any old 32 MiB batch. Its 12 warmups and original
raw evidence remain local. The offline verifier also checks this separate batch.
