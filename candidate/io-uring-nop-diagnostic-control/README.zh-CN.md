# io_uring NOP 诊断接口成本

本 bundle 保存 `io_uring/nop.c` 的整理后裸机结果和精确 workload。它尚未发送上游，且
当前不建议作为性能回归报告发送：NOP 是用于测试 io_uring 提交/完成框架的控制 opcode，
不是应用数据 I/O；引入成本的提交也在有意扩展 registered file/buffer 测试能力。

release screen 使用 fresh-boot
`v6.12.95 A -> v7.1.3 -> v6.12.95 B`，精确提交对照使用
`aa00f67adc2c -> a85f31052bce`：

| 对照 | plain NOP | `INJECT_RESULT` | 配对 ratio |
| --- | ---: | ---: | ---: |
| release | `+15.801%` | `+16.240%` | `+0.378%` |
| 精确提交 | `+8.311%` | `+8.692%` | `+0.357%` |

两组 drop-first 方向一致；release 最大 CV `0.717%`，精确提交最大 CV `1.707%`。全部
比较点实际运行 `preempt=full`，语义、CQE、ring-health 和 direct-hit 检查均通过。
两类 profile 几乎同比变慢，因此信号来自共同 NOP 路径，而不是注入结果特有逻辑。

精确提交增加 flags、资源检查和 registered file/buffer 测试状态处理，只解释 release
差异的一部分。继续二分后续 NOP 测试能力的现实收益不足，故按 stop rule 收口。

- [`bare-metal/`](bare-metal/)：release 与精确提交摘要、运行身份和 direct-hit；
- [`reproducer/`](reproducer/)：原样 raw-UAPI workload、runner 和 validator。
