# 裸机结果

## 证据索引

本目录按证据类型而不是日期拆分文件。逐项核对后，没有两份 TSV 承担完全相同的证据作用，
也没有哪一份可以安全地按重复文件删除：

| 分组 | 文件 | 作用 |
| --- | --- | --- |
| 精确主结果 | [`source-identity.tsv`](source-identity.tsv)、[`build-identity.tsv`](build-identity.tsv)、[`exact-ab-points.tsv`](exact-ab-points.tsv)、[`measured-rounds.tsv`](measured-rounds.tsv)、[`result-summary.tsv`](result-summary.tsv) | 源码/构建来源、逐点统计、入选的逐轮数据和跨区间汇总 |
| 范围与 direct hit | [`slot-gradient.tsv`](slot-gradient.tsv)、[`standalone-cross-check.tsv`](standalone-cross-check.tsv)、[`mechanism-summary.tsv`](mechanism-summary.tsv) | 槽位范围、可读源码交叉验证和精确路径计数 |
| Cache 与 allocation 诊断 | [`node-cache-cold-warm.tsv`](node-cache-cold-warm.tsv)、[`node-cache-trace.tsv`](node-cache-trace.tsv)、[`first-fill-allocation-diagnostics.tsv`](first-fill-allocation-diagnostics.tsv) | reuse timing、推断 hit、新 slab/perf 与两项 prefill 诊断 |
| 协作者补丁 | [`uzair-patch1-followup.tsv`](uzair-patch1-followup.tsv)、[`uzair-v2-bulk-refill.tsv`](uzair-v2-bulk-refill.tsv)、[`uzair-v2-bulk-refill-mechanism.tsv`](uzair-v2-bulk-refill-mechanism.tsv) | 专用 slab/accounting 诊断，以及修订 bulk-refill 的计时和路径验证 |

汇总表与逐轮表会有少量统计值重叠：前者是快速索引，后者是重新计算 mean、CV 和
drop-first 所需的最小原始数据。计时和 trace 的测量范围不同，不能为了减少文件数而混成
同一种结果。

主结果是围绕
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
（`io_uring/rsrc: get rid of per-ring io_rsrc_node list`）的精确 direct-parent
对照。三次独立启动顺序为：

```text
e410ffca5886 parent A -> 7029acd8a950 child -> e410ffca5886 parent B
```

每点先运行 3 轮 warm-up，再运行 15 轮 measured；每轮以 QD 64 完成 4,096 次
fixed-file 安装。端到端 ns/install 均值为：

| 点 | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 102.476 | 0.705% |
| child | 114.441 | 0.289% |
| parent B | 102.576 | 0.340% |

child 相对两个 parent 控制中点慢 `11.621%`。每点删除首个 measured round 后为
`11.649%`，parent 漂移仅 `0.097%`。45 行 measured 数据全部通过 CQE、slot 分配、
sentinel、affinity 和 unexpected-result 检查。

三点使用相同 normalized config、GCC 15.2.0、Kbuild metadata、模块签名 key、等长
release string 和完全相同的 workload binary；实际运行抢占模式均为 `full`。紧凑身份和
逐点统计见 [`build-identity.tsv`](build-identity.tsv) 与
[`exact-ab-points.tsv`](exact-ab-points.tsv)。[`measured-rounds.tsv`](measured-rounds.tsv)
保留了 45 行入选的逐轮 timing，可复算 mean、CV、drop-first 和 semantic-pass；无需携带
完整 raw runner workspace。

## 辅助比较

另有两组独立 matched sandwich 得到相同方向：

- Linux 6.13 相对 Linux 6.12 控制中点慢 `10.747%`；
- Linux 7.1.3 相对 Linux 6.12.95 控制中点慢 `15.602%`。

它们只负责 release-level 复核，不能替代精确提交结果；所有正式点的实际运行模式也都是
`preempt=full`。三组比较集中在 [`result-summary.tsv`](result-summary.tsv)。

## 精简源码交叉验证

422 行的 F0-only 精简源码也在精确内核上按 parent A、child、parent B 三次独立启动：

| 点 | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 101.239 | 0.440% |
| child | 114.010 | 1.776% |
| parent B | 103.917 | 0.441% |

child 相对 parent 中点慢 `11.145%`，drop-first 为 `11.147%`；45 行数据全部通过
语义门，各点实际抢占模式均为 `full`。另一次不计时的 256-operation smoke trace 中，
`io_msg_ring()`、`io_msg_install_complete()` 和 `__io_fixed_fd_install()` 均精确命中
256 次。

两次 parent 控制漂移为 `2.645%`，略高于正式 `2%` 门槛，因此本结果只保留为紧凑的
方向性交叉验证，不晋级为第二组正式证据。数据见
[`standalone-cross-check.tsv`](standalone-cross-check.tsv)。

## 槽位数适用范围检查

在同一组精确内核上，再按 parent A、child、parent B 顺序分别测试 64、256、1,024 和
4,096 个 target slot，queue depth 始终为 64。child 相对匹配 parent 中点分别慢
`11.853%`、`8.717%`、`8.399%` 和 `11.889%`；180 行 measured 数据全部通过语义检查。

64 槽位的 child 与 256 槽位的 parent 控制噪声稍高，因此本轮只用于校准适用范围，
不替代正式的 4,096 槽位结果。它说明同方向信号在一批 64 次操作时已经存在，并非只有
超大 fixed-file table 才会出现；该幅度也没有随表大小单调增加。数据见
[`slot-gradient.tsv`](slot-gradient.tsv)。

## 非计时机制 trace

独立 parent/child trace 在两边各执行 128 次成功 SEND_FD 安装。
`io_msg_ring()`、`io_msg_install_complete()` 和 `__io_fixed_fd_install()` 都是 128 次；
固定文件安装内部嵌套的 `io_rsrc_node_alloc()` 则从 parent 的 0 次变为 child 的 128 次，
总调用数从 4 变为 129。

这证明 workload 直接命中了新增的逐安装 resource-node 路径，但不能据此断言全部时间差都
来自某一个 allocator 函数。计数见 [`mechanism-summary.tsv`](mechanism-summary.tsv)。

## Node-cache cold/warm 诊断

后续在相同机器上比较新 target ring 首次填充（cold）与同 ring 先填充、注销、再填充
（warm）。选择 128 槽是因为 v7.1.3 的 `IO_ALLOC_CACHE_MAX` 为 128。只有最后 128 次
填充处于计时窗口；每个统计行汇总 128 个独立子对，即每条件 16,384 次计时安装。
执行顺序为 v7.1.3 A、v6.12.95、v7.1.3 B，每点独立启动。

| 点 | cold ns/install | warm ns/install | warm 对 cold |
| --- | ---: | ---: | ---: |
| v7.1.3 A | 139.531 | 109.143 | `-21.779%` |
| v6.12.95 | 106.357 | 105.030 | `-1.248%` |
| v7.1.3 B | 136.046 | 110.143 | `-19.040%` |

v7.1.3 中点为 `137.789 -> 109.643 ns/install`（`-20.427%`）。独立非计时 trace
确认 cold 为 128 次 fresh node allocation、推断 hit 为 0，warm 为 0 次 fresh、推断
hit 为 128。
两个 v7.1.3 控制的 cold 漂移为 `-2.498%`、warm 漂移为 `+0.916%`；因此本轮定位为
机制诊断，不替代控制漂移仅 `0.097%` 的精确提交正式证据。两个 v7.1.3 点各自的
cold/warm 效应均约为 19%～22%。
因此 cache 确实改善 reuse，但原 workload 每轮新建 ring，首次填充没有可复用 node。v7.1.3
warm 仍比 6.12.95 cold 慢 `3.089%`；本轮没有继续拆分剩余成本。紧凑数据见
[`node-cache-cold-warm.tsv`](node-cache-cold-warm.tsv) 与
[`node-cache-trace.tsv`](node-cache-trace.tsv)。

## 专用 slab patch 后续

私人协作者提供了一枚 patch，把 `io_rsrc_node` 的 fresh allocation 改为来自专用
`kmem_cache`。附件原样应用到 public v6.18-rc4 `6146a0f1dfae`，child 为
`72461b3d32e3`。六次独立冷启动比较未打 patch、默认 SLUB 合并的 patch，以及同一 patched
binary 加全局 `slab_nomerge`：

| workload | 未打 patch | patch 默认 | 相对未打 patch | patch `slab_nomerge` | 相对未打 patch |
| --- | ---: | ---: | ---: | ---: | ---: |
| F0 4,096-slot first fill | 119.394 | 129.586 | `+8.536%` | 129.721 | `+8.649%` |
| 128-slot cold | 127.401 | 137.154 | `+7.656%` | 136.934 | `+7.483%` |
| 128-slot warm reuse | 108.677 | 108.261 | `-0.382%` | 108.463 | `-0.197%` |

默认 patch cache 被合入 `:A-0000032`；使用 `slab_nomerge` 时则成为独立命名 cache。
两者结果几乎相同，说明默认是否合并不能解释约 8% 的 cold slowdown。warm reuse 没有该
slowdown，因为它从 per-ring cache 取得 node，不进入 slab allocator。

第二组四点实验让两边都使用 `slab_nomerge`，只从 patched cache 删除
`SLAB_ACCOUNT`。account commit 为 `72461b3d32e3`，direct child `82669ceb64b7`
只包含这一行源码变化：

| workload | 保留 `SLAB_ACCOUNT` | 删除该 flag | 变化 |
| --- | ---: | ---: | ---: |
| F0 4,096-slot first fill | 130.326 | 118.099 | `-9.382%` |
| 128-slot cold | 137.338 | 125.207 | `-8.833%` |
| 128-slot warm reuse | 108.139 | 108.657 | `+0.479%` |

两边命名 cache 都是 object size 24、alignment 32、order 0、每 slab 128 个对象、aliases 0。
F0 account/no-account 控制漂移为 `-0.318%` 和 `-0.044%`，drop-first 仍为
`-9.375%`。本轮把 patch 新增的 cold 成本隔离到 `SLAB_ACCOUNT` 启用的工作，但没有继续
拆分具体 memory-cgroup 指令。删除 flag 是诊断，不是修复建议；no-account 数值也不与另一个
六点序列中的未打 patch 数值混算。见
[`uzair-patch1-followup.tsv`](uzair-patch1-followup.tsv)。

## 修订后的 v2 slab 与 bulk-refill 补丁

Uzair 随后提供了两枚 v2 补丁。patch 1 保留专用 `io_rsrc_node` slab，但删除非故意加入的
`SLAB_ACCOUNT`；patch 2 在 cache miss 后按 32 个对象一批补充 per-ring node cache。
两枚附件均原样应用到 public v6.18-rc4 `6146a0f1dfae`；本地测试提交分别为 patch 1
`da4febd3fde7` 与 patch 1+2 `2359f858fa8d`。

正式实验使用六次 fresh boot：

```text
unpatched-A -> patch1-A -> patch1+2-A -> patch1+2-B -> patch1-B -> unpatched-B
```

每点沿用原 4,096-slot first-fill workload，3 轮 warm-up、15 轮 measured，实际运行模式均为
`preempt=full`：

| role | ns/install | 相对 unpatched | 相对 patch 1 | 控制漂移 |
| --- | ---: | ---: | ---: | ---: |
| unpatched | 119.187760 | — | — | `+0.661%` |
| patch 1 | 118.406665 | `-0.655%` | — | `-0.171%` |
| patch 1+2 | 118.415243 | `-0.648%` | `+0.007%` | `-0.872%` |

90 行 primary measured 数据全部通过 workload 语义检查，最大 CV 为 `1.301%`。因此在该
边界上 patch 1 基本持平，patch 2 相对 patch 1 没有可测增益；drop-first 复核给出同一
解释。这里的 current-base 数值不能与早先 v6.12.95→v7.1.3 的 `+15.602%` release
对照相减，因为两者基线不同，回答的问题边界也不同。

次级 128-slot cold 诊断中 patch 1+2 相对 patch 1 慢 `1.404%`，但 patch 1+2 自身控制
漂移达到 `2.786%`。因此只如实保留 noisy 结果，不据此判断 patch 2 变慢；warm 诊断也没有
显示 bulk-refill 收益。包含 drop-first 与两项诊断的紧凑计时数据见
[`uzair-v2-bulk-refill.tsv`](uzair-v2-bulk-refill.tsv)。

另一次独立的**非计时**探针证明，中性计时结果不是因为没有命中 patch 2。两套内核都执行
73,985 次 `io_rsrc_node_alloc()`；patch 1 产生 73,985 次单对象 slab allocation，
patch 1+2 则改为 2,313 次 bulk call。每次都请求并完整返回 32 个对象，没有 short 或 zero
return。256-slot smoke cycle 精确调用 8 次；18 个完整 4,096-slot cycle 中每个都精确调用
128 次。bulk 共返回 74,016 个对象，比消耗的 73,985 个 node 多 31 个；它们是 source
ring 首次 refill 后未使用的余量。这证明预期 bulk 路径完整执行，但仍不能解释 allocator
往返大幅减少为何没有降低端到端 first-fill 时间。见
[`uzair-v2-bulk-refill-mechanism.tsv`](uzair-v2-bulk-refill-mechanism.tsv)。

## 首次填充 allocation 路径诊断

三轮连续诊断进一步收窄了首次填充成本，但不改变正式的 `+11.621%` 精确提交结果。

第一轮在精确 `7029acd8` child 上做非计时 trace：4,096 次
`io_rsrc_node_alloc()` 中，workload PID 的 40 次 `allocate_slab()` 恰好有 32 次动态嵌套
在 node allocation 内，而且全部使用同一 cache pointer。这与 4,096 个对象除以每个
order-0 slab 的 128 个对象完全一致。独立整进程 perf sandwich 中，child 相对 parent
中点的 cycles 和 instructions 分别为 `+6.999%`、`+8.063%`；L1D miss 只能看方向，LLC
数据不可用。这证明首次填充确实创建新 slab backing、child 总 CPU 工作更多，但不能量化
32 次 slab 页创建单独解释了多少 clean latency。

第二轮构造 `7029acd8` 的诊断性直接 child，在 sparse table 注册时提前分配并缓存全部
4,096 个 raw node。后续 SEND_FD first fill 仍执行 4,096 次逻辑 node allocation，但非计时
门禁确认其动态范围内 `allocate_slab()` 为 0。干净 fresh-boot 顺序为
`child A -> prefill -> child B`：

| 点 | ns/install | CV |
| --- | ---: | ---: |
| child A | `115.289095` | `1.084%` |
| 诊断 prefill | `104.245882` | `0.338%` |
| child B | `115.071126` | `1.723%` |

child 中点为 `115.180111 ns/install`；prefill 将计时成本减少 `10.934228 ns/install`
（`-9.493%`）。child 端点漂移为 `-0.189%`，drop-first 为 `-9.456%`。因此，逐对象 raw
allocation 与清零路径解释了本轮首次填充额外成本的大部分。但它没有把 32 次 slab 页创建
与其余 4,096 次 `kzalloc()` 工作分开，也没有证明总成本下降：注册时间和缓存内存只是移到
计时区外，且没有测量。本变体仅用于诊断，不是上游修复。

第三轮使用更窄的诊断：只提前准备 slab backing，计时 first fill 仍保留 4,096 次逐对象分配。
注册阶段从真实 `io_rsrc_node_alloc()` callsite 分配 4,129 个对象，释放 4,096 个，并保留
33 个 page anchor。非计时 probe 在注册阶段观察到 32 次 `allocate_slab()`。计时 first fill
仍执行 4,096 次 `io_rsrc_node_alloc()` 和 4,096 次逐对象 kmalloc，但 `allocate_slab()` 为 0。
干净 fresh-boot 顺序为 `child A -> slab-prime A -> slab-prime B -> child B`：

| 点 | ns/install | CV |
| --- | ---: | ---: |
| child A | `115.055192` | `0.943%` |
| slab-prime A | `116.418701` | `1.632%` |
| slab-prime B | `115.258854` | `1.205%` |
| child B | `114.494531` | `0.345%` |

child 中点为 `114.774862 ns/install`，slab-prime 中点为 `115.838778 ns/install`。该诊断为
`+0.927%`（`+1.063916 ns/install`），没有改善计时 first fill。child 和 slab-prime 端点漂移
分别为 `-0.487%` 与 `-0.996%`，drop-first 为 `+1.050%`。在这项诊断下，可以更窄地排除
新 slab page 创建是主要原因；但不能把结果解释成纯粹的逐页成本，因为注册阶段还分配、触碰
了对象并保留 anchor。

综合 full-prefill 与 slab-prime，后续值得研究的是反复执行的逐对象分配、清零，以及首次触碰
或局部性影响。现有结果不能说明其中哪项成本最大，两种变体也都不是降低总成本的修复。三步
紧凑数据合并保存在
[`first-fill-allocation-diagnostics.tsv`](first-fill-allocation-diagnostics.tsv)。两项精确诊断源码
增量分别为
[`full-prefill`](../reproducer/0001-diagnostic-prefill-sparse-node-cache.patch) 和
[`slab-prime`](../reproducer/0002-diagnostic-prime-node-slab-backing.patch)。

## 平台与范围

物理机为 Intel Core i7-12700KF、32 GiB RAM。单进程固定到 P-core 逻辑 CPU 2，governor
和 EPP 都为 `performance`，Turbo 关闭。

本证据只讨论经 `IORING_MSG_SEND_FD` 触发的 fixed-file registration/update；不声称普通
io_uring read/write 提交、应用性能或所有 fixed-file 更新场景都发生同样回归。
