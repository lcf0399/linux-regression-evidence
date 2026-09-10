# io_uring MSG_RING SEND_FD fixed-file 安装

本证据包记录一条很窄的 io_uring registration/update slowdown。workload 使用
`IORING_OP_MSG_RING` 与 `IORING_MSG_SEND_FD`，把 source ring 中的一个 fixed file
安装到另一个 ring 的 sparse fixed-file table 空槽中。它是聚焦的 synthetic
microbenchmark，不是应用 benchmark，也不声称普通 io_uring read/write fast path 存在
同样回归。

## 主结果

正式证据是围绕
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
（`io_uring/rsrc: get rid of per-ring io_rsrc_node list`）的精确 direct-parent 对照：

| 点 | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | `102.476` | `0.705%` |
| child | `114.441` | `0.289%` |
| parent B | `102.576` | `0.340%` |

child 相对 parent 控制中点慢 `11.621%`；drop-first 为 `11.649%`，parent 漂移仅
`0.097%`，45 行 measured 数据全部通过语义检查。三个 fresh-boot 点使用同一 workload
binary、归一化配置、构建控制、CPU 2，实际运行抢占模式均为 `full`。

该提交为每个 fixed-file slot 引入独立 resource node，以消除 per-ring serialization 和
资源回收停滞。因此本证据描述的是可测的 registration/update trade-off，不建议回退这项
正确性/可扩展性变更。

## v3 补丁验证（2026-09-10）

三枚作者原补丁在精确 public v6.18-rc4 上完成验证。原 first fill 为
`107.731 ns/install`，对未修改版快 `9.791%`、对 0001+0002 快 `8.819%`。
独立同 ring 的 4096 文件移除→重装区间分别快 `18.820% / 18.584%`；
独立计数证明扩大缓存避免后续批次反复分配。

注册开始至首填完成没有改善：原辅助程序 `+1.144%`，新 ring 间隔版 `+1.637%`。
少量使用/小表未过门项均保留；注册 4096 只用 64 后仍有 4032 个闲置 node。
未修改版也有退出工作跨轮现象，但尚未证明它造成此前高 CV。
[v3 结果页](bare-metal/uzair-v3-prefill/README.zh-CN.md)集中保存全部 28 项计时、
机制验证、身份与辅助源码；不宣称完整生命周期或普通 I/O 性能收益。

## 证据链

| 问题 | 结果 | 范围 |
| --- | --- | --- |
| release-level 信号是否复现？ | Linux 6.13 对 6.12：`+10.747%`；Linux 7.1.3 对 6.12.95：`+15.602%` | 匹配 endpoint 的辅助复核 |
| 是否只有 4,096 槽才出现？ | 64–4,096 槽均为 `+8.399%`～`+11.889%` | 范围检查；小点更吵 |
| workload 是否命中新路径？ | 128 次固定文件安装内嵌套的 `io_rsrc_node_alloc()` 从 `0` 变为 `128` | 非计时 direct-hit trace |
| 后续 node cache 为什么没关闭首次填充成本？ | v7.1.3 同 ring reuse 从 `137.789` 降至 `109.643 ns/install`（`-20.427%`） | cache 改善 reuse；新 ring 的 cache 为空 |
| 专用 slab 是否有用？ | 初版私人 patch 使 cold first fill 慢约 8%；删除非故意的 `SLAB_ACCOUNT` 后该新增成本消失 | 仅为诊断；记账语义不能随意删除 |
| 修订后的 32-object bulk refill 是否有用？ | patch 1：`-0.655%`；patch 1+2 对 patch 1：`+0.007%` | 干净 current-base 计时持平；非计时 probe 证明 bulk 完整执行 |
| 首次填充是否创建新 slab 页？ | 4,096 次 node allocation 内恰好嵌套 32 次 `allocate_slab()` | 机制证明，不是 clean latency 归因 |
| 这些新 slab 页是否解释了差距？ | 预热 slab backing 后，计时 first fill 中的 32 次新 slab 调用全部消失，但结果为 `+0.927%` | 没有加速；4,096 次逐对象分配仍然保留 |
| 大部分计时成本在哪里？ | 把全部 4,096 个 raw node 的分配/清零移到注册阶段后，first fill 减少 `10.934 ns/install`（`-9.493%`） | 诊断 prefill；未测注册时间和常驻内存 |

两项 prefill 诊断把这些成本区分得更清楚。slab prime 把新页创建移出计时区，但保留全部
4,096 次逐对象分配，延迟没有改善；full prefill 把 raw allocation 与清零全部移出计时区，
结果快 `9.493%`。这把后续研究范围收窄到反复执行的逐对象分配、清零及首次触碰或局部性影响，
但不能证明其中哪一项成本最大。两项诊断都没有证明总成本下降，因为注册工作和常驻内存不在
主测量范围内。

详细数字、边界、路径计数和合并后的 allocation 诊断见
[`bare-metal/`](bare-metal/README.zh-CN.md)。两项精确诊断源码增量也已收录以便重放，并明确
不是上游修复提案。

## 目录

- [`bare-metal/`](bare-metal/README.zh-CN.md)：精确 A/B、范围检查、cache/allocation
  诊断、协作者补丁结果、紧凑身份和入选的逐轮数据；
- [`reproducer/`](reproducer/README.zh-CN.md)：正式源码、可读 F0-only 源码、cold/warm
  helper、validator、runner 与两项预填充诊断源码增量；
- [`upstream-status/`](upstream-status/)：引入线程、后续 cache 变更和有日期的源码审计；
- `email/`：本地往来邮件和发送记录；被 Git 忽略，不属于公开证据包。
