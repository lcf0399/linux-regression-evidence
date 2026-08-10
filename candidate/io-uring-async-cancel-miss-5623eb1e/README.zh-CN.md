# io_uring 异步取消 miss-path 回归

本证据包记录
[`5623eb1ed035`](https://github.com/torvalds/linux/commit/5623eb1ed035f01dfa620366a82b667545b10c82)
（`io_uring/tctx: add separate lock for list of tctx's in ctx`）引入的一条很窄的
io_uring 异步取消 slowdown。

主 `A0_MISS` workload 先保持真实 eventfd poll 请求处于 pending，再提交
`user_data` 保证不存在的 `IORING_OP_ASYNC_CANCEL`。计时区包含 cancel SQE 准备、提交、
等待与 CQE 收集；紧接着在计时区外校验 `-ENOENT`。poll 建立和清理同样不计时。匹配的
`A0_HIT` profile 取消真实 key，作为命中路径对照。
两者的 completion 形状不同，因此只分别比较各自的新旧变化，不相减绝对值。

fresh-boot 精确 direct-parent 夹心中，guaranteed-miss 路径在 child 上慢 `8.833%`，
而匹配的 hit-path control 为 `-0.562%`：

| profile | parent A ns/attempt | child | parent B | child vs parent midpoint |
| --- | ---: | ---: | ---: | ---: |
| `A0_MISS` | 143.660 | 156.137 | 143.271 | `+8.833%` |
| `A0_HIT` control | 131.641 | 130.324 | 130.480 | `-0.562%` |

`A0_MISS` 删除首个 measured round 后为 `+8.807%`；parent 漂移 `-0.271%`，最大
CV `0.422%`。90 行 measured 数据全部通过语义检查。所有比较点实际运行模式都是
`preempt=full`，并使用相同 normalized config 和 workload binary。

另一次使用 373 行简版 standalone reproducer 的实验在同一 miss path 上得到
`+9.099%`，独立印证原样源码的结果。正式结论仍绑定产生原始数据的 1341 行源码。

该提交同时改变 tctx 列表锁、遍历和任务状态处理。因此证据只能识别整个精确提交，不能
把差异归因到某一次 mutex 操作，也不建议回退这项 correctness 变更。这是聚焦的
synthetic microbenchmark，不是应用 benchmark，也不声称成功取消、批量取消、同步取消或
teardown 取消都发生了回归。

## 目录

- [`bare-metal/`](bare-metal/)：精确 A/B、入选逐轮数据、构建身份和非计时 direct-hit
  计数；
- [`reproducer/`](reproducer/)：正式测量使用的原样 raw-UAPI 源码、供阅读的简短注释版
  standalone reproducer，以及 validator；
- [`upstream-status/`](upstream-status/)：引入线程及有日期的重复报告/后续修复审计。
