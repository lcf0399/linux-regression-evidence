# 裸机结果

精确比较围绕
[`5623eb1ed035`](https://github.com/torvalds/linux/commit/5623eb1ed035f01dfa620366a82b667545b10c82)
及其唯一 direct parent `fc5ff2500976`。三次独立启动顺序为：

```text
fc5ff2500976 parent A -> 5623eb1ed035 child -> fc5ff2500976 parent B
```

每点先运行 3 轮不计时 warm-up，再运行 15 轮 measured。每轮 `A0_MISS` 包含 64 批、
每批 32 次 guaranteed-miss cancel，共 2,048 次。每批开始前先证明 32 个 eventfd poll
仍处于 pending；计时区只提交 32 个不存在 key 的取消请求并收集其 `-ENOENT` CQE；真实
poll 的清理在计时结束后进行。`A0_HIT` 保持相同基本形状，但取消真实 poll key。
两个 profile 只分别做跨内核比较，不把它们的绝对 latency 相减。

| profile | parent A | child | parent B | child vs midpoint | drop first | parent drift | max CV |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `A0_MISS` | 143.660 | 156.137 | 143.271 | `+8.833%` | `+8.807%` | `-0.271%` | `0.422%` |
| `A0_HIT` control | 131.641 | 130.324 | 130.480 | `-0.562%` | `-0.533%` | `-0.882%` | `0.490%` |

单位为 ns/attempt。90 行 measured 数据全部通过预期返回值、CQE、pending 状态、overflow、
timeout、未完成请求和 CPU affinity 检查。可复算统计的入选逐轮数据见
[`measured-rounds.tsv`](measured-rounds.tsv)，紧凑统计见
[`result-summary.tsv`](result-summary.tsv)。

三点使用相同 normalized config、GCC 15.2.0、Kbuild metadata、等长 release string 和
同一 workload binary。每点 requested/build/actual preempt 都是 `none/dynamic/full`，
所以实际运行模式匹配为 `full`。物理机为 Intel Core i7-12700KF、32 GiB RAM；进程固定
到 P-core 逻辑 CPU 2，governor 与 EPP 为 `performance`，Turbo 关闭。紧凑身份见
[`build-identity.tsv`](build-identity.tsv)。

## 简版 standalone 交叉验证

另行构建的 373 行 standalone reproducer 也按 fresh-boot parent A -> child ->
parent B 顺序运行。三点均值依次为 141.661、154.102 和 140.838 ns/attempt；child
相对 parent 中点慢 `+9.099%`。drop-first 为 `+9.108%`，parent 漂移 `-0.581%`，
最大 CV `0.374%`。45 行 measured 数据全部通过语义检查，实际抢占模式匹配为 `full`。

该结果独立印证原样源码的正式结果（`+8.833%`），但不取代它。紧凑数据见
[`standalone-cross-check.tsv`](standalone-cross-check.tsv)。

## 非计时 direct-hit

child 的 `A0_MISS` trace 中，`io_async_cancel()`、`io_try_cancel()` 和
`io_poll_cancel()` 各命中 256 次；`A0_HIT` 中各命中 128 次。这证明两个 profile 都
进入必需取消路径。可选探针 `io_cancel_req_match()` 为 0，不是必需 gate。计数见
[`direct-hit.tsv`](direct-hit.tsv)。

## 归因边界

精确提交同时增加 `ctx->tctx_lock`、改变 tctx 列表同步和遍历，并在异步取消慢路径恢复
`TASK_RUNNING`。因此本结果只能把窄 miss-path slowdown 归因到整个提交，不能隔离为某
一条锁指令的成本，也不能据此回退 correctness 变更。
