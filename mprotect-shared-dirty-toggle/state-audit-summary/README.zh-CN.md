# State Audit 摘要

这个目录保留 `shared_dirty_full_toggle_64m` workload 的 lab state-shape audit
公开精简摘要。

完整 runner 目录、raw CSV/JSON、launch logs、`pipeline_run_env.json` 和
`execution_order.json` 已经移到本地忽略的归档目录：

```text
mprotect-shared-dirty-toggle/local-archive/20260520-state-audit-lab-raw/
```

这不是 timing evidence。它用于检查 `v6.12.77`、`v6.19.9` 和
`akpm/mm mm-unstable 444fc9435e57` 在这个 workload 下是否操作同一种用户态
mapping 状态。

精简结论是：

- 成功 run 都保持同一种 4 KiB shared-dirty PTE mapping 形态；
- protect/restore 前后都是 16384 个 present pages；
- 没有观察到 THP backing；
- 所有成功 run 都是 `expected_match_ratio=100` 且 `unexpected_results=0`。

完整文字总结见 `summary-20260520.zh-CN.md`，小型机器可读表见
`state-shape-summary.csv`。
