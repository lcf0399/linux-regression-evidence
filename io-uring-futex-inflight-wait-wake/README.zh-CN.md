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

## 上游回复与候选补丁验证

Jens Axboe 认可同步完成的 WAKE 不需要采用这套 inflight tracking，并给出两枚补丁：
补丁 1 把 tracking 移到 WAIT 专用 prep，补丁 2 只对包含 private wait 的请求保留
tracking。

我以冻结的 2026-07-23 Linus master `48a5a7ab8d6a` 为基线，对原始两枚附件执行了
如下五点 fresh-boot 验证：

```text
baseline A -> patch 1 A -> 完整两补丁 -> patch 1 B -> baseline B
```

baseline 中点为 `196.616 ns/pair`，patch 1 中点为 `189.948 ns/pair`，完整系列为
`189.900 ns/pair`。patch 1 相对 baseline 中点快 `3.392%`，完整系列快 `3.416%`；
完整系列相对 patch 1 中点只变化 `-0.025%`，符合本轮 private-futex 参数的预期。
75 行正式计时全部通过，五点实际抢占模式均为 `preempt=full`。

这轮补丁验证使用的源码基线晚于上面的 direct-parent 实验，两组百分比不能相减，也不能
视为使用同一个分母。

随后又增加严格配对的 shared-futex 模式，比较 patch 1 A、完整系列和 patch 1 B。
完整系列相对 patch 1 中点快 `1.578%`，drop-first 同为 `1.578%`；patch 1 控制漂移
`0.319%`，最大 CV `0.200%`，45 行计时全部通过。这直接命中 patch 2 取消 inflight
tracking 的标量 shared-WAIT 路径；shared WAITV 未测试。

## 目录

- [`bare-metal/`](bare-metal/)：紧凑的精确 A/B 结果和构建身份；
- [`reproducer/`](reproducer/)：正式实验源码和较短、带注释的标量 standalone；
- [`upstream-status/`](upstream-status/)：正确性原线程以及发送前排重/修复审计。
- [`bare-metal/jens-patch-validation.tsv`](bare-metal/jens-patch-validation.tsv)：紧凑的
  五点补丁验证结果。
- [`bare-metal/jens-patch2-shared-validation.tsv`](bare-metal/jens-patch2-shared-validation.tsv)：
  patch 2 的配对 shared-futex 结果。
- [`bare-metal/jens-patch-identity.tsv`](bare-metal/jens-patch-identity.tsv)：两枚附件的
  精确哈希以及应用后的 commit/tree 身份。
