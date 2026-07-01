# Formal Lab 摘要

这个目录保留 `mprotect_shared_dirty_formal_refresh` workload 最早 formal lab
run 的公开精简摘要。

原始 runner 目录、raw CSV/JSON、`pipeline_run_env.json` 和 `execution_order.json`
已经移到本地忽略的归档目录：

```text
mprotect-shared-dirty-toggle/local-archive/20260513-formal-lab-raw/
```

## Timing

主指标是 `shared_dirty_full_toggle_64m` 场景的 `cycle_ns_per_page`，数值越小越快。

| CPU | v6.12.77 | v6.19.9 | 说明 |
| ---: | ---: | ---: | --- |
| 1 | 346.8 | 578.1 | clean reliable |
| 2 | 394.7 | 641.7 | robust-only |
| 4 | 381.1 | 624.8 | partial same direction；一个 v6.12.77 run 发生 QEMU failure |

`summary.csv` 是机器可读的精简表。

## Coverage

direct-hit coverage 是和 clean timing 分开收集的，所以 timing 表不是 coverage
instrumented 结果。见 `coverage-summary.csv`。
