# mprotect 真机支持结果

这些紧凑表格保留辅助证据，但不替代精确 commit-level 归因：

| 文件 | 作用 | 主要结果 |
| --- | --- | --- |
| `release-window-summary.csv` | 缩小版本窗口 | `v6.16=25.000`、`v6.17=37.000 ns/page`，slowdown 首次出现在 v6.17 |
| `single-protect-summary.csv` | 检查单次 protection change | `v6.16=8.000`、`v6.17=14.000 ns/page` |
| `folio-order-summary.csv` | base-page 状态门禁 | 9 行均为 4 KiB order-0 pages，没有 compound/THP backing |

详细历史 runner 与中间日志只在本地实验归档中保留。
