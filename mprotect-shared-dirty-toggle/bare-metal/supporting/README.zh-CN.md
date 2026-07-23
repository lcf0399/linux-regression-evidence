# mprotect 真机支持证据

这里保存仍有独立作用、但不承担最终 commit-level 归因的真机结果。

| 目录 | 作用 | 主要结果 |
| --- | --- | --- |
| `20260623-narrow-6.16-6.19-3rounds/` | release-window narrowing | `v6.16=25.000`、`v6.17=37.000 ns/page`，slowdown 首次出现在 `v6.17` |
| `20260630-single-protect-followup/` | 排除 repeated-toggle 特有现象 | 单次 protect 为 `v6.16=8.000`、`v6.17=14.000 ns/page` |
| `20260706-folio-order-check/` | 页状态语义门禁 | 9 轮均为 4 KiB、order-0 base pages，无 compound/THP |

这些结果补充说明版本窗口、调用形状和页状态。当前 culprit 与机制结论仍以
`../20260721-cac1db8c3aad-exact-ab/` 和
`../20260722-cac1-folio-batch-decomposition-ab/` 为准。
