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

fresh-boot 精确 direct-parent 夹心中，child 相对 parent 中点慢 `8.091%`；独立 424 行
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

引入提交增加的是 memory-cgroup accounting。本证据不建议回退资源记账，只询问能否在
保留同一 accounting 语义的同时降低逐请求开销。

## 目录

- [`bare-metal/`](bare-metal/)：紧凑的精确 A/B 数据与身份信息；
- [`reproducer/`](reproducer/)：精确正式源码与更短、带注释的 standalone；
- [`upstream-status/`](upstream-status/)：原补丁线程及按日期记录的排重/修复审计。
