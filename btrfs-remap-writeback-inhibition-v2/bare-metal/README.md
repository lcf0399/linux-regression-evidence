# Bare-Metal Validation

The public result is the three-point matched control sandwich:

```text
control A -> upstream v2 -> control B
```

Each point used a fresh Btrfs filesystem on `/dev/ram0`, 15 rounds, 10,000
operations per scenario, 4 KiB ranges, CPU 2, and the `performance` governor.
The control and v2 builds used the same Linux 7.1.0 source commit, normalized
config, compiler, preemption contract, and reproducible Kbuild metadata.

## Timing

| scope | scenario | control midpoint ns/op | v2 ns/op | v2 delta |
| --- | --- | ---: | ---: | ---: |
| all rounds | `FICLONERANGE` 4 KiB | 2957.118 | 2159.123 | **-26.986%** |
| all rounds | `FIDEDUPERANGE` 4 KiB | 3541.199 | 2762.835 | **-21.980%** |
| skip first | `FICLONERANGE` 4 KiB | 2961.755 | 2156.813 | **-27.178%** |
| skip first | `FIDEDUPERANGE` 4 KiB | 3550.278 | 2764.666 | **-22.128%** |

Detailed aggregates are in `per-run-stats.tsv` and `control-sandwich.tsv`.
The boot/run order and semantic gates are in `matrix-summary.tsv`.

## Direct Hit

The separate direct-hit run used 1,000 clone plus 1,000 dedupe operations on
the v2 kernel.  `direct-hit.tsv` records 2,000 calls to
`btrfs_remap_file_range`, 7,473 to `btrfs_inhibit_eb_writeback`, and 2,033 to
`btrfs_uninhibit_all_eb_writeback`; no required target was missing and the
workload reported zero unexpected results.

`btrfs_inhibit_claim_slot` is listed as a non-required diagnostic target in the
TSV.  It was not present as an independently traceable function in this build,
so its zero count is not a direct-hit failure.

## Identity

`source-identity.tsv` records the exact source, upstream message, patch hash,
config hash, and kernel releases.  `matched-build.tsv` records the matched
control/v2 build audit.  Local absolute build paths and rebuildable full logs
are intentionally excluded.
