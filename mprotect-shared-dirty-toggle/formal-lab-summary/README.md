# Formal Lab Summary

This directory keeps the compact public summary for the original lab run of
the `mprotect_shared_dirty_formal_refresh` workload.

The raw runner directories, raw CSV/JSON, `pipeline_run_env.json`, and
`execution_order.json` were moved to the ignored local-only archive:

```text
mprotect-shared-dirty-toggle/local-archive/20260513-formal-lab-raw/
```

## Timing

The main metric is `cycle_ns_per_page` for the `shared_dirty_full_toggle_64m`
scenario. Smaller is faster.

| CPU | v6.12.77 | v6.19.9 | note |
| ---: | ---: | ---: | --- |
| 1 | 346.8 | 578.1 | clean reliable |
| 2 | 394.7 | 641.7 | robust-only |
| 4 | 381.1 | 624.8 | partial same direction; one v6.12.77 QEMU failure |

See `summary.csv` for the compact machine-readable table.

## Coverage

Direct-hit coverage was collected separately from clean timing, so the timing
table above is not coverage-instrumented. See `coverage-summary.csv`.
