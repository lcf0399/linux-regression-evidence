# mm-unstable Follow-up Summary

This directory keeps the compact follow-up summary for Pedro's small-folio
`mprotect()` optimization in `mprotect-shared-dirty-toggle`. Full runner
workspaces, raw CSV/JSON, and launch logs have been moved to the local-only
ignored `../local-archive/` directory and are not part of the public evidence
bundle.

## Scope

- baseline kernels: `v6.12.77`, `v6.19.9`
- follow-up kernel: `akpm/mm mm-unstable 444fc9435e57`
- mm-unstable release string: `7.1.0-rc3-mm-unstable-444fc9435e57`
- workload: `shared_dirty_full_toggle_64m`
- primary metric: `cycle_ns_per_page`, lower is better
- repetitions: 9
- order: interleaved
- coverage: disabled

## Conclusion

The lab matrix shows partial mitigation for this workload, but not a return to
the `v6.12.77` fast range. The `16 CPU` row has one `v6.12.77` QEMU failure and
is supporting trend evidence only. The local matrix is noisier and is kept only
as follow-up context.

## Files

- `summary.csv`: compact public table for the primary metric.
