# 上游状态审计

历史审计日期：2026-07-30；当前源码与线程状态刷新日期：2026-08-30。

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

因此证据仍值得发送，但应视为窄、优先级中等的性能反馈。合适路径是回复原补丁线程，而不是
新发 broad regression report；回复保留 accounting 目标，只询问能否降低每次 WAITV
分配的开销。另一条已解决的 inflight-WAKE 回归不会解决这项 allocation 差异。2026-08-30
在 Linux v7.2 的 `io_uring/futex.c` 上运行 `get_maintainer.pl`，得到 Jens Axboe、io-uring
和 linux-kernel；公开文件边界也已审计。回复已经准备好，但尚未发送。
