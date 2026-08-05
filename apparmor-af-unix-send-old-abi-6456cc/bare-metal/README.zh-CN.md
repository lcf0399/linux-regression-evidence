# 裸机结果

主表来自本 bundle 中简短 standalone 源码的独立运行。每点 fresh boot，先做 3 轮
warm-up，再做 15 轮 measured；每轮用 2,048 次 `sendmmsg()` 发送 65,536 条消息。
计时区只包含系统调用，setup、对端排空和校验均不计时。

实验 pair 在同一合成 merge 基线上隔离上游 `6456ccbd2ff7` 的精确源码增量：

- parent：`0bfa1c2da7a88d4e5fcd65044a3076f8d672f3a4`；
- child：`30cb02a874b4c62191decf55c3971e45a5173d50`；
- 顺序：parent A、child、parent B。

| 来源/profile | 实际 preempt | parent A | child | parent B | child vs 中点 | drop-first | parent 漂移 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| standalone `sendmmsg()` | none | 343.382 | 395.831 | 343.259 | `+15.295%` | `+15.289%` | `-0.036%` |
| formal `sendmmsg()` | full | 341.915 | 393.803 | 343.910 | `+14.841%` | `+14.863%` | `+0.584%` |
| formal io_uring SEND | full | 314.070 | 373.614 | 319.424 | `+17.953%` | `+17.939%` | `+1.705%` |

单位为 ns/message 或 ns/op，越低越好。两份源码使用各自完全匹配、但不同的实际
抢占模式，因此分开裁决，百分比不混算。standalone 最大 CV 为 `0.137%`；formal
直接 `sendmmsg()` 和 io_uring SEND 最大 CV 分别为 `0.154%` 与 `0.110%`。所有
选中行都通过语义检查。

作为路径佐证，另一组 v6.16.12 A -> v6.17.13 -> v6.16.12 B 的 `perf` 夹心中，
`security_unix_may_send()` children overhead 依次为 `0.27%`、`6.77%`、
`0.27%`；新点的采样调用栈继续经过 `apparmor_unix_may_send()`、
`aa_unix_peer_perm()` 和 `unix_peer_perm()`。这是一组 release-endpoint 观察，
不与上面的精确 pair 计时表混算，也不是提交内部消融。

实验机为 Intel Core i7-12700KF、32 GiB RAM。各点固定 P-core CPU 2，
`intel_pstate` governor 与 EPP 均为 `performance`，Turbo 关闭。formal 三点使用
同一归一化内核配置、GCC 15.2.0、Kbuild 元数据、模块签名密钥和 workload binary；
standalone 三次启动也使用同一 binary。其源码 SHA-256 为
`71544093a1b948404ed186f36870515867602b2c9696207dea6e6238c5904704`，
`run-identity.tsv` 记录的 binary SHA-256 为
`42f90b0ea82867ad10cf8aa2101d5079194e620081795de2f1d946ac408f4d80`。

formal 源码还包含多个 io_uring profile，但其中 `u0` 对照只计时 `sendmmsg()`，
对端排空在计时区外。简短源码彻底去掉 io_uring setup，并独立复现该结果。
