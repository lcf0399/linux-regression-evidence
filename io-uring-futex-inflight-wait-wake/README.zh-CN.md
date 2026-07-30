# io_uring 标量 futex wait/wake inflight-tracking 回归

本证据包记录一条很窄的标量 io_uring futex wait/wake slowdown，精确引入提交为
[`079afb081c42`](https://github.com/torvalds/linux/commit/079afb081c4288e94d5e4223d3eb6306d853c68b)
（`io_uring/futex: mark wait requests as inflight`）。

正式 raw-UAPI workload 使用一个 ring 和 32 个按 cacheline 分隔的 private futex
word。每个计时 cycle 先提交 32 个标量 `IORING_OP_FUTEX_WAIT`，再提交 32 个标量
`IORING_OP_FUTEX_WAKE`，最后恰好回收 64 个 CQE。每个 wait 必须返回 0，每个 wake
必须返回 1。

fresh-boot direct-parent 夹心结果如下；child 相对 parent 中点慢 `9.268%`：

| 实现 | parent A ns/pair | child | parent B | child vs midpoint |
| --- | ---: | ---: | ---: | ---: |
| 正式源码 | 180.079 | 196.647 | 179.856 | `+9.268%` |
| 338 行 standalone | 180.171 | 197.856 | 179.502 | `+10.020%` |

正式结果 drop-first 为 `+9.269%`、parent 漂移 `-0.124%`、最大 CV `0.169%`；
standalone 的 drop-first 为 `+9.996%`、parent 漂移 `-0.371%`。90 行标量计时数据
全部通过 CQE、返回值、timeout、overflow、未完成请求和 CPU affinity 检查。所有比较
内核的实际运行抢占模式均为 `preempt=full`。

匹配的 `WAITV -> WAKE` profile 只变化 `+1.385%`，未过预注册的 5% 信号门。因此当前
claim 只限上述标量 wait/wake 形状。

引入提交通过让 pending private-futex wait 在其 `mm` 状态消失前可被退出取消路径找到，
从而修复 use-after-free 风险。标量 WAIT 和 WAKE 都使用 `io_futex_prep()`，因此 child
中每个 measured pair 的两侧都会执行新增 tracking call。本证据不建议回退这项 correctness
fix；上游问题只应是：能否用更低的逐请求成本保留同样的生命周期保证。

这是 focused synthetic microbenchmark，不是应用 benchmark，也不声称 generic futex、
generic io_uring 或 wait-vector 路径都发生回归。

## 目录

- [`bare-metal/`](bare-metal/)：紧凑的精确 A/B 结果和构建身份；
- [`reproducer/`](reproducer/)：正式实验源码和较短、带注释的标量 standalone；
- [`upstream-status/`](upstream-status/)：正确性原线程以及发送前排重/修复审计。
