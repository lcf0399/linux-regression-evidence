# io_uring MSG_RING SEND_FD fixed-file 安装

本证据包记录一条很窄的 io_uring registration/update slowdown。workload 使用
`IORING_OP_MSG_RING` 与 `IORING_MSG_SEND_FD`，把 source ring 中的一个 fixed file
安装到另一个 ring 的 fixed-file table 空槽中。

主证据是围绕
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
（`io_uring/rsrc: get rid of per-ring io_rsrc_node list`）的精确 direct-parent
对照：

| 点 | mean ns/install |
| --- | ---: |
| parent A | 102.476 |
| child | 114.441 |
| parent B | 102.576 |

child 相对 parent 控制中点慢 `11.621%`；各点删除首个 measured round 后为
`11.649%`，parent 漂移仅 `0.097%`。三次启动使用完全相同的 workload binary、
归一化内核配置和实际运行抢占模式（`full`）。45 行 measured 数据全部通过语义检查。

同一组精确内核上的 64、256、1,024、4,096 target-slot 适用范围检查在每个规模都得到
同方向信号（`+8.399%`～`+11.889%`）。较小点噪声更高，因此它不替代主结果；它只说明
信号并非局限于原来的 4,096 槽位压力形状。

非计时 trace 确认直接命中预期路径：128 次成功安装中，嵌套在
`__io_fixed_fd_install()` 内的 `io_rsrc_node_alloc()` 调用由 parent 的 0 次变为
child 的 128 次。这能证明 direct hit，但不能把全部时间差都归因到某一个 allocator
函数。

后续 node-cache 提交 `ed9f3112a8a8` 已包含在 Linux 7.1.3 中；匹配的 Linux
6.12.95/7.1.3 对照仍得到同方向信号（`+15.602%`）。原变更解决了 per-ring
serialization 与资源回收停滞，因此本证据描述的是 registration-time trade-off，
不建议回退原变更。

2026-08-22 的 cold/warm 诊断解释了 cache 没有关闭原 workload 差距的原因：新 target
ring 首次填充为 128/128 fresh node allocation、推断 cache hit 为 0；同 ring 先填充
并注销后，第二次填充推断为 128/128 cache hit。v7.1.3 cold/warm 中点为
`137.789 -> 109.643 ns/install`
（`-20.427%`）；6.12.95 同形状预热仅为 `-1.248%`。所以 cache 能改善 reuse，但不能
改善新 ring 的首次填充；它消除了该窄诊断中的大部分 release 差距，但没有完全消除。

后续私人协作 patch 尝试把 `io_rsrc_node` 放进专用 slab。在 public v6.18-rc4 上，
4,096 槽首次填充的未打 patch、默认合并 patch 与 `slab_nomerge` patch 中点分别为
`119.394`、`129.586`、`129.721 ns/install`；两个 patch 点分别慢 `8.536%` 和
`8.649%`，而 128 槽 warm reuse 与未打 patch 的差异均小于 `0.4%`。因此本机结果不支持
用默认 slab 合并解释该变化。

另一组单变量诊断让两边都使用 `slab_nomerge`，只改变 `SLAB_ACCOUNT`。删除该 flag 后，
首次填充变化 `-9.382%`、128 槽 cold fill 变化 `-8.833%`，warm reuse 为 `+0.479%`。
这把 patch 新增的 cold-path 成本收口到该 flag 启用的记账工作，但不是修复建议：删除 flag
会改变 memory-cgroup 记账语义。私人 patch 本身未在本证据包中再分发；紧凑比较见
[`uzair-patch1-followup.tsv`](bare-metal/uzair-patch1-followup.tsv)。

Uzair 修订后的 v2 系列在 patch 1 中删除了非故意加入的记账 flag，并在 patch 2 中增加
32-object bulk refill。六次启动的 current-base 序列测得 patch 1 相对 unpatched
`-0.655%`，patch 1+2 相对 patch 1 `+0.007%`；在该边界上都属于基本持平。独立非计时
探针确认 patch 2 完整执行 2,313 次 32-object bulk call，且每个完整 4,096-slot cycle
精确调用 128 次。因此中性结果不能用“没有命中分支”或“bulk 部分返回”解释。正式计时、
noisy 的 128-slot 诊断和机制计数分别保存在 [`bare-metal/`](bare-metal/) 中。

这是一条聚焦的 synthetic microbenchmark，不是应用 benchmark，也不声称普通
io_uring read/write fast path 存在相同回归。

为了便于代码审阅，证据包另提供一份带注释、仅保留 F0 的精简源码。它在同一组精确
内核上的独立交叉验证得到 `+11.145%`，与正式结果 `+11.621%` 接近；但两次 parent
控制漂移为 `2.645%`，超过正式 `2%` 门槛，因此只能作为方向性复核。正式结论仍绑定
未修改的 1,165 行完整源码和上表结果。

## 目录

- [`bare-metal/`](bare-metal/)：精确 A/B、槽位数适用范围、node-cache cold/warm 与私人
  patch 诊断、紧凑身份信息及 direct-hit trace summary；
- [`reproducer/`](reproducer/)：正式实验源码、精简 `SEND_FD` standalone、cold/warm
  诊断、trace helper、validator 与单点 runner；
- [`upstream-status/`](upstream-status/)：引入线程、后续 cache 变更和有日期的源码审计；
- `email/`：本地邮件材料与发送说明；被 Git 忽略，不属于公开证据包。
