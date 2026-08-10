# 裸机精确 A/B

测试日期：2026-07-30。

顺序为 fresh-boot
`816095894c0f parent A -> 6e0d71c288fd child -> 816095894c0f parent B`。
两提交是精确 direct-parent；每个点使用相同的归一化配置、GCC 15.2.0、Kbuild 元数据、
等长 release string、CPU 策略和 workload 二进制。

实验机为 Intel Core i7-12700KF、32 GiB RAM，计时固定 P-core CPU 2。governor 和 EPP
均为 `performance`，Turbo 关闭；三个点实际 preemption mode 均为 `full`。命令行虽请求
`preempt=none`，但硬门记录并只比较实际模式一致的点。

每个实现、每次启动有三轮预热和 15 个 measured round；每轮运行 512 个 cycle，每个
cycle 含八个 WAITV/wake pair，共 4,096 pair。初始化和残留 waiter 检查不计时。

| 实现 | parent A | child | parent B | child vs midpoint | drop first | parent drift | max CV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 正式实现 | 1045.687 | 1128.704 | 1042.755 | `+8.091%` | `+8.080%` | `-0.280%` | `0.151%` |
| standalone | 1040.270 | 1110.916 | 1046.198 | `+6.488%` | `+6.511%` | `+0.570%` | `0.142%` |

90 个 WAITV measured round 全部通过语义检查。正式 child trace 观察到 128 次
`io_futexv_prep()`、128 次 `io_futexv_wait()`、128 次 `io_futexv_complete()`，并命中
预期 wake 路径；standalone child smoke 分别观察到 16 次 WAITV 函数和 32 次 wake。

匹配的正式标量对照变化为 `-0.555%`，不属于本回归 claim。
