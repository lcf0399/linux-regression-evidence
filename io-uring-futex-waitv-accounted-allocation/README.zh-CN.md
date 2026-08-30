# io_uring WAITV accounted-allocation 回归

本证据包记录一条很窄的 io_uring futex `WAITV -> WAKE` slowdown，引入提交为
[`6e0d71c288fd`](https://github.com/torvalds/linux/commit/6e0d71c288fdcf5866f5d0c6cde850a091cc3c55)
（`io_uring/futex: use GFP_KERNEL_ACCOUNT for futex data allocation`）。它与直接
parent 的唯一源码差异，是把每个 WAITV 的数据分配从 `GFP_KERNEL` 改为
`GFP_KERNEL_ACCOUNT`。

正式 raw-UAPI workload 使用一个 ring、八个独立 wait vector，每个 vector 包含八个
cacheline 分离的 private futex word。每个计时 cycle 提交八个
`IORING_OP_FUTEX_WAITV`，唤醒每个 vector 的第 3 项并检查 16 个 CQE；计时区外再用一次
wake 证明每个 vector 剩余七个 waiter 已被清理。

fresh-boot 精确 direct-parent 夹心中，child 相对 parent 中点慢 `8.091%`；另一份 424 行
standalone 复现为 `6.488%`：

| 实现 | parent A ns/pair | child | parent B | child vs midpoint |
| --- | ---: | ---: | ---: | ---: |
| 正式源码 | 1045.687 | 1128.704 | 1042.755 | `+8.091%` |
| standalone | 1040.270 | 1110.916 | 1046.198 | `+6.488%` |

正式实现 drop-first 为 `+8.080%`、parent drift 为 `-0.280%`、最大 CV 为
`0.151%`；standalone drop-first 为 `+6.511%`、parent drift 为 `+0.570%`。
两个实现共 90 个 WAITV measured round 全部通过语义与状态检查；所有比较内核实际均为
`preempt=full`。

匹配的标量 `WAIT -> WAKE` 对照变化为 `-0.555%`，所以结论只限于上述 WAITV
准备/分配路径，不代表 generic futex 或 io_uring 性能。

在未修改 v7.2 上又执行了一轮当前基线诊断：其直接 child 只把同一分配诊断性改回
`GFP_KERNEL`。no-account child 相对两个 v7.2 控制点中点快 `8.123%`；另一份
standalone 快 `8.998%`，标量对照仅变 `+0.089%`。控制漂移绝对值不超过
`0.865%`，所有 CV 低于 `0.15%`。因此，后续源码变化截至 v7.2 尚未消除这项窄
WAITV allocation 成本。当前诊断不建议取消 memcg accounting，也不替代上面的精确
引入提交证据。紧凑结果见
[`bare-metal/current-v72-diagnostic-summary.tsv`](bare-metal/current-v72-diagnostic-summary.tsv)。

引入提交增加的是 memory-cgroup accounting。本证据不建议回退资源记账，只询问能否在
保留同一 accounting 语义的同时降低逐请求开销。

2026-08-30 的上游刷新显示，Linus master `08dbfad3f504` 和 io_uring 维护者
`for-next` `44e96e364b04` 都仍保留这项 `GFP_KERNEL_ACCOUNT` 分配；原补丁线程仍无回复，
也未发现等效性能优化。公开证据与回复原补丁线程的窄邮件均已准备好，但回复尚未发送。另一条
已经修复的 futex inflight-WAKE 回归属于不同成本，不会解决这里的 WAITV allocation 差异。

## 目录

- [`bare-metal/`](bare-metal/)：紧凑的精确 A/B 数据与身份信息；
- [`reproducer/`](reproducer/)：精确正式源码与更短、带注释的 standalone；
- [`upstream-status/`](upstream-status/)：原补丁线程及按日期记录的排重/修复审计。
