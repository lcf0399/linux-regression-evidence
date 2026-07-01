# mm-unstable follow-up summary

这个目录保留 `mprotect-shared-dirty-toggle` 针对 Pedro small-folio `mprotect()`
optimization 的轻量 follow-up 汇总。原始 runner workspace、raw CSV/JSON 和 launch
logs 已移到本地忽略的 `../local-archive/`，不作为 public evidence bundle 上传。

## 范围

- baseline kernels：`v6.12.77`、`v6.19.9`
- follow-up kernel：`akpm/mm mm-unstable 444fc9435e57`
- mm-unstable release string：`7.1.0-rc3-mm-unstable-444fc9435e57`
- workload：`shared_dirty_full_toggle_64m`
- primary metric：`cycle_ns_per_page`，越低越好
- repetitions：9
- order：interleaved
- coverage：disabled

## 结论

lab matrix 显示 mm-unstable 对该 workload 有部分缓解，但没有回到 `v6.12.77`
的快区间。`16 CPU` 行有一次 `v6.12.77` QEMU failure，因此只作为辅助趋势证据。
local matrix 噪声更大，只保留为 follow-up context。

## 文件

- `summary.csv`：公开保留的主指标汇总表。
