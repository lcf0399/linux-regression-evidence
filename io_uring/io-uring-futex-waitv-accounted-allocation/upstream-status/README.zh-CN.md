# 上游状态审计

## 2026-10-19跟踪核对

regzbot于10月13日以缺少反馈、很可能是微基准问题为由停止跟踪，标记为inconclusive。
这是跟踪处置，不是已找到修复，也不是判断正确性所需的记账可以取消。
[原报告条目及完整状态区分](../../../upstream-status/README.zh-CN.md)。

## 2026-07-30与08-30历史源码／线程审计

以下源码与检索结论截至2026-08-30，没有据本次跟踪更新声称重新核对所有最新分支。

Jens Axboe 于 2026-01-25 以
`[PATCH for-next] io_uring/futex: use GFP_KERNEL_ACCOUNT for futex data allocation`
提交引入补丁；公开归档中该线程没有回复。补丁说明的目标是让这笔分配参与 memory-cgroup
accounting。

该补丁进入 Linux 7.0 的 io_uring core pull request。2026-08-30 重新核对的 Linus
master `08dbfad3f504` 和 io_uring 维护者 `for-next` `44e96e364b04`，都仍在
`io_futexv_prep()` 的可变长数据分配中使用 `GFP_KERNEL_ACCOUNT`。后续源码虽已改用
`kzalloc_flex()`，并合入了另一条取消同步 WAKE inflight tracking 的修复，但这些变化没有
取消 WAITV 的 memcg-accounted allocation。按原线程、精确提交、WAITV、accounting 和
performance 重新检索，仍未发现等效性能优化或相同性能报告；原补丁线程仍无归档回复。

技术问题是能否在保留 accounting 目标的同时降低每次 WAITV 分配的开销。
另一条已解决的 inflight-WAKE 回归不会解决这项 allocation 差异。
