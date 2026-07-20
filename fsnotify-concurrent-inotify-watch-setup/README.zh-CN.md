# fsnotify 并发 inotify watch 增删回归

状态：上游新报告候选；尚未发送。

本证据包记录一条范围很窄但可重复的回归：Linux 提交
[`94bd01253c3d`](https://github.com/torvalds/linux/commit/94bd01253c3d5b1cd8955bdadeed24af02088094)
（`fsnotify: Track inode connectors for a superblock`）使并发 inotify watch 建立与撤销
变慢。

## 精确 parent/child 结果

主实验在裸机上按以下顺序完成三次独立启动：

```text
6c790212c588 parent A
-> 94bd01253c3d child
-> 6c790212c588 parent B
```

P8 distinct-inode aggregate worker-time 指标在 child 上相对两端 parent 分别慢
`17.65%` 和 `15.11%`，两次 parent 自身只漂移 `2.18%`。matched distinct/shared 比值分别恶化 `17.67%` 和
`21.64%`，parent 漂移为 `3.32%`。

计时 case 在计时区外创建 96 个独立 inotify instance 和 96 个不同文件 inode；随后 8 个
固定到不同 P-core 的 worker thread 并发地为每个 inode 增加一个 watch，再将其撤销。
matched shared-inode control 保留相同的 96 个 inotify instance 和 pathname，但所有 pathname
都是同一个 inode 的 hard link；一个 keeper watch 保证 connector 的创建与销毁不进入计时区。
每次启动先运行 2 个 warm-up round，再运行 25 个正式 round。

existing-mask 与 path-lookup 负控的变化均小于 `0.3%`；P8 shared-inode case 在 child 上还
略快，因此这不是整机变慢或 generic inotify 变慢。紧凑结果见
[`bare-metal/exact-ab-summary.tsv`](bare-metal/exact-ab-summary.tsv)。

## 并发度扩展

第二组精确 parent/child/parent 夹心用不同物理 P-core 测试了 P6 和 P8，并以 8 个 P-core
的全部 16 个 SMT thread 作为辅助压力点。P6 distinct 在 child 上相对两端 parent 分别慢
`9.03% / 9.56%`，distinct/shared ratio 恶化 `10.75% / 10.85%`；同时间窗 P8 又独立复现
absolute `+19.58% / +19.11%`、paired `+17.62% / +16.80%`。parent drift 均低于
`1.4%`，shared-inode 和负控没有同向 slowdown。

较早的 exact 运行中，P1、P4 distinct 变化都低于 `2.1%`。两组结果把这台机器、96-watch
workload 的材料性信号起点放在 P4 与 P6 之间；这不是普适的应用或 CPU 阈值。W16-SMT 的
absolute 指标同方向，但 child paired CV 为 `15.006954%`，刚好高于预注册 `15%` gate，
因此只作辅助证据。数据见
[`bare-metal/scaling-extension-summary.tsv`](bare-metal/scaling-extension-summary.tsv)。

## 聚焦机制证据

两个同 commit 诊断 build 拆分 per-superblock connector list 维护的两部分：

- `lock-only` 保留 `list_lock` 获取/释放，但移除 list pointer update；
- `nolist` 同时移除 list 操作及其 lock pair。

在五次独立启动的 `full -> lock-only -> full -> nolist -> full` 序列中，P8 distinct 在
`lock-only` 中恢复 `14.27%`，在 `nolist` 中恢复 `21.43%`；paired 指标分别恢复
`12.70%` 和 `20.46%`。这支持 list mutation/额外持锁时间和裸 lock handoff 都有材料性
成本。两种 build 只是归因 probe，不是安全修复候选。

数据见
[`bare-metal/focused-mechanism-summary.tsv`](bare-metal/focused-mechanism-summary.tsv)。

## 真实软件拓扑 gate

精确计时 benchmark 是 synthetic workload，因此又用独立 trace 检查原版用户态软件能否形成
同一种 state shape。Ubuntu `inotify-tools 4.25.9.0-1` 中 8 个未经修改的
`inotifywait -m -r` 进程，分别递归监控一份真实 Linux 7.1.3 源码树的 8 个互不重叠子树；
这些目录都在同一个 ext4 superblock 上。

8 个进程共建立 4177 个目录 watch。每个进程观察到的 `fsnotify_add_mark_locked()`、
`fsnotify_inode_mark_connector` allocation 和
`fsnotify_detach_connector_from_object()` 次数都与自己的 watch 数逐项相等。add-mark 活动
出现在 9 个 CPU 上，1 ms 时间桶中最多同时出现全部 8 个 watcher PID。

这个 gate 只建立现实拓扑和目标内核路径的桥接，不提供应用级 timing。8 个 watcher 同时启动
是有意安排的压力形状，但工具、递归遍历、源码树和 inotify 语义均未修改。详见
[`real-topology/README.zh-CN.md`](real-topology/README.zh-CN.md)。

## 最新上游状态审计

2026-07-20 的审计覆盖最新正式版 `v7.1.4`、`v7.2-rc4`、Linus `master` 的
`1590cf032971`、`next-20260717`，以及 Jan Kara 的 linux-fs `fsnotify`、`for_next` 和
`for_linus` 分支。所有已检查树的 connector 创建和摘除路径仍保留
`94bd01253c3d` 引入的 `list_lock` 与 `list_add()`/`list_del()`；这些行的 blame 仍指向
`94bd01253c3d`。

对精确操作及关键符号的历史检索没有发现后续优化或等效修复。唯一命中的后续提交是
`a05fc7edd988`；它在卸载时消费 connector list，并未消除 watch 增删成本。对官方
linux-fsdevel 与 regressions public-inbox 截至 7 月 20 日的检索，也没有发现针对该成本的
后续补丁或既有 `94bd` 回归报告。

这是一项源码历史与 prior-art 审计，不表示后续内核的运行时 slowdown 百分比必然与 exact
parent/child 相同。精确 ref 与审计边界见
[`upstream-status/README.zh-CN.md`](upstream-status/README.zh-CN.md)。

## claim 边界与上游取舍

这里不声称所有 fsnotify 事件投递、所有 inotify 用户或某个具体应用都变慢。性能 claim 只限
精确 commit 前后并发增删 distinct-inode watch 的场景。

`94bd01253c3d` 引入的 connector list 被后续
[`a05fc7edd988`](https://github.com/torvalds/linux/commit/a05fc7edd988c176491487ef0ae4dbf5f7a64cd7)
用于在卸载时高效销毁稀疏 inode mark，并支撑相关对象生命周期/竞态修复。因此本证据不建议
直接 revert；提交上游要询问的是，能否在保留正确性与 sparse-unmount 收益的前提下，降低
常见 watch 增删路径的成本。

原 patch 讨论主要聚焦卸载效率和 inode 生命周期正确性。v2 review 曾提到 lockless RCU
遍历，但当时认为 inode notification mark 的增删通常不频繁，因此不预期出现明显锁竞争。
本次精确 A/B 检查的正是并发 watcher 初始化下的这条增删路径。在已审阅的系列讨论中没有找到
这条路径此前的 timing 结果。

参考：[v3 cover](https://lore.kernel.org/linux-fsdevel/20260121135513.12008-1-jack@suse.cz/)、
[v2 locking discussion](https://lore.kernel.org/linux-fsdevel/20260123-mengenlehre-wildhasen-46e47a6e7558@brauner/)、
[维护者回复](https://lore.kernel.org/linux-fsdevel/m5a3dyhvpnjhyjmxae2o2sd2azhynbrupmhzsy2fbgomhdcyow@imnv6ytjaxfi/)。

## 目录

- [`bare-metal/`](bare-metal/README.zh-CN.md)：精确 A/B 构建身份及紧凑 timing/scaling/机制摘要；
- [`reproducer/`](reproducer/README.zh-CN.md)：standalone 语义 workload 与 distinct/shared
  paired runner；
- [`real-topology/`](real-topology/README.zh-CN.md)：stock `inotifywait` trace 摘要及重跑脚本。
- [`upstream-status/`](upstream-status/README.zh-CN.md)：最新正式版、主线、linux-next、维护者树
  与邮件列表 prior-art 审计。
