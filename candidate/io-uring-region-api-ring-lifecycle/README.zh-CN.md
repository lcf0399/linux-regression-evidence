# io_uring region API 标准 ring 短生命周期成本

本证据包记录一个范围很窄的 io_uring 性能变化：反复创建和销毁标准 ring 时，
v7.1.3 相对 v6.12.95 慢 `10.832%`。两组直接 parent/child 裸机对照进一步确认，
将 SQ 和 CQ 内部存储转换到 region API 的两个相邻提交分别增加 `4.563%` 和
`3.019%` 的完整生命周期成本。

上游状态：**尚未发送**。如果决定发送，再进行当前上游排重、修复审计和收件人核对。

## 核心结果

release screen 使用 fresh-boot
`v6.12.95 A -> v7.1.3 -> v6.12.95 B`：

| old A | new | old B | new vs old midpoint | drop first | old drift | max CV |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8299.366 | 9121.969 | 8161.576 | `+10.832%` | `+10.761%` | `-1.660%` | `2.171%` |

单位为 ns/lifecycle。精确提交对照为：

| 转换 | 直接 parent -> child | parent A | child | parent B | child vs midpoint |
| --- | --- | ---: | ---: | ---: | ---: |
| SQ | `02255d55260a` -> `8078486e1d53` | 8447.193 | 8874.057 | 8526.431 | `+4.563%` |
| CQ | `8078486e1d53` -> `81a4058e0cd0` | 8851.177 | 9155.872 | 8924.005 | `+3.019%` |

两个相邻比率顺序相乘为 `+7.719%`；从首组 parent 中点到最终 child 为
`+7.884%`。它们解释了 release signal 的主要部分，但 release 与精确提交测试位于
不同源码基线，不能把百分比机械相减后声称得到了精确的剩余成本。

## Workload

正式 `L0_STANDARD_LIFECYCLE` workload 不依赖 liburing，直接使用 UAPI。每个计时事件：

1. `io_uring_setup()` 创建 64-entry 标准 ring；
2. 映射标准 ring 和 SQE 区域；
3. 提交并验证一个 `IORING_OP_NOP`；
4. 解除映射并 `close()` ring fd。

每点先运行 3 个 warm-up round，再运行 15 个 measured round；每轮包含 512 个
lifecycle。事件之间用于等待异步清理的暂停位于计时区外。该指标覆盖用户态可见的完整
setup/mmap/NOP/munmap/close 路径，不是 `io_create_region()`、`io_free_region()` 或
某个 `memmap.c` helper 的独立函数耗时。

## 结论边界

- 这是合法、可复现的短生命周期 synthetic microbenchmark，不是应用 benchmark；
- 长期复用一个 ring 的常见应用不会反复支付这笔 setup/teardown 成本；
- 两枚提交都修改多个 io_uring 文件，因此结论停在 **whole-commit attribution**；
- region API 具有统一生命周期管理等工程收益，本证据不建议回退，只用于询问标准 ring
  是否还能保留更轻量的创建销毁路径。

## 目录

- [`bare-metal/`](bare-metal/)：release screen、两组精确提交对照、逐轮数据、身份和
  direct-hit 摘要；
- [`reproducer/`](reproducer/)：正式测量使用的原样 raw-UAPI workload、runner、合同和
  validator。
