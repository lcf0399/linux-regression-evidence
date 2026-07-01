# State Audit Summary

This directory keeps the compact public summary for the lab state-shape audit
of the `shared_dirty_full_toggle_64m` workload.

The full runner directories, raw CSV/JSON, launch logs, `pipeline_run_env.json`,
and `execution_order.json` were moved to the ignored local-only archive:

```text
mprotect-shared-dirty-toggle/local-archive/20260520-state-audit-lab-raw/
```

This is not timing evidence. It checks whether `v6.12.77`, `v6.19.9`, and
`akpm/mm mm-unstable 444fc9435e57` operate on the same userspace mapping state
for this workload.

The compact conclusion is:

- successful runs kept the same 4 KiB shared-dirty PTE mapping shape;
- protect/restore state kept 16384 present pages;
- no THP backing was observed;
- `expected_match_ratio=100` and `unexpected_results=0` for all successful runs.

See `summary-20260520.md` for the full prose summary and
`state-shape-summary.csv` for the small machine-readable table.
