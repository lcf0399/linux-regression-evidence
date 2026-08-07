# 上游状态审计

审计日期：2026-07-30。

Jens Axboe 于 2026-01-25 以
`[PATCH for-next] io_uring/futex: use GFP_KERNEL_ACCOUNT for futex data allocation`
提交引入补丁；公开归档中该线程没有回复。补丁说明的目标是让这笔分配参与 memory-cgroup
accounting。

该补丁进入 Linux 7.0 的 io_uring core pull request。审计时 Linus master
`fc02acf6ac0c` 的 `io_futexv_prep()` 仍使用 `GFP_KERNEL_ACCOUNT`。之后相关源码只见
机械式 allocation helper 转换和无关的 partial-wake correctness 修复；未发现等效性能
优化或相同性能报告。

因此合适的上游路径是回复原补丁线程，而不是新发 broad regression report。回复应明确
保留 accounting 目标，只询问能否降低每次 WAITV 分配的开销。
