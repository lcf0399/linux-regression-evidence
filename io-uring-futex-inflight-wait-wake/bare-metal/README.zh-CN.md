# 裸机精确 A/B

实验日期：2026-07-29。

fresh-boot 顺序为
`6a8118a77eec parent A -> 079afb081c42 child -> 6a8118a77eec parent B`，两笔
提交是精确 direct-parent pair。所有点使用同一份归一化配置、GCC 15.2.0、Kbuild
元数据、等长 release string、CPU policy 和 workload binary。

实验机为 Intel Core i7-12700KF、32 GiB RAM；计时固定在 P-core CPU 2。scaling
governor 和 EPP 均为 `performance`，Turbo 关闭，三个点的实际运行抢占模式均为
`full`。启动参数虽然请求 `preempt=none`，但硬门记录并比较实际模式，只有 matched
点才允许进入计时。

正式标量 workload 每个启动点先做 3 轮 warm-up，再做 15 轮 measured；每轮执行
512 个 cycle、每个 cycle 32 对 wait/wake，即 16,384 对。计时区包含 SQE 准备、提交
和 CQE 回收，不包含 ring 创建和内存准备。

| 实现 | parent A | child | parent B | child vs midpoint | drop first | parent drift | max CV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 正式源码 | 180.079 | 196.647 | 179.856 | `+9.268%` | `+9.269%` | `-0.124%` | `0.169%` |
| standalone | 180.171 | 197.856 | 179.502 | `+10.020%` | `+9.996%` | `-0.371%` | `0.250%` |

90 行标量计时数据全部通过语义与状态检查。child 上的非计时 trace 按预期命中
`io_futex_prep()`、`io_futex_wait()`、`io_futex_wake()` 和
`io_futex_complete()`。精确 child 源码在 `io_futex_prep()` 中新增
`io_req_track_inflight()`；由于该 helper 可能被内联，没有把它设为必须单独出现的
trace symbol。

匹配的正式 `WAITV -> WAKE` profile 只变化 `+1.385%`，未过 5% 信号门，不属于当前
回归 claim。
