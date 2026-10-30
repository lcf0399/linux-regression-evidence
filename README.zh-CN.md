# Linux 性能回归证据仓库

这个仓库保存经过整理的 Linux 性能回归证据，以及对应的上游 follow-up 和 patch
验证材料。

下述上游状态为标明日期的快照，相关来源链接见各场景材料。

## 按子系统查找

13份证据材料按内核子系统归类。

| 目录 | 场景数 | 内容 |
| --- | ---: | --- |
| [fs/](fs/) | 4 | Btrfs、fsnotify、kernfs、tmpfs xattr |
| [mm/](mm/) | 2 | mprotect；换出回退页序 |
| [io_uring/](io_uring/) | 3 | MSG_RING SEND_FD、标量 futex、WAITV |
| [kernel/sched/](kernel/sched/) | 3 | fair 默认时间片、延迟重新入队管道交接、RT sysctl |
| [security/](security/) | 1 | AppArmor AF_UNIX |

[candidate/](candidate/)保存另外4个候选。
tmpfs归入文件系统、AppArmor归入安全子系统；不是仅按benchmark调用的接口或研究目录命名分类。

## 已发送或已进入上游线程

状态快照日期为2026-10-10，主线源码参考为
[`3857c2fe5449`／v7.3-rc6](https://github.com/torvalds/linux/tree/3857c2fe5449541e24afc5efdb0f81a8a8f9a3a0)相关源码。
下列11个已报告问题主题均有裸机证据。
3类已有对应主线修复；fsnotify接受维护者的取舍后结题，另7类尚无确认解决。
不等于3枚由我们编写的补丁被合入。2026-10-10已将tmpfs、Btrfs、标量futex和fsnotify
四条线全部结题，不再默认跟进或追加实验，证据继续保留。
tmpfs修复在6月15日已进入主线，早于7月6日报告；当时被测7.1正式版仍不含它。
另已核实它首次包含于8月17日发布的v7.2主线正式版，未核实更旧stable分支的回移情况。
以下日期使用北京时间。

| 证据 | 上游状态 | 当前技术状态 |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mm/mprotect-shared-dirty-toggle/) | [公开线程](https://lore-kernel.gnuweeb.org/regressions/7c9eeb21.1c04f.19efaacbb2f.Coremail.chengfenglin%40stu.xmu.edu.cn/t/)仍以我们7月24日的补充结束；未找到更晚公开回复或对应修复。 | 精确 A/B 将 `39.77%` slowdown 归因到 `cac1db8c3aad`。嵌套诊断从 generic single-PTE update/flush 处理和 normal page/folio lookup 合计回收 `87.29%` 缺口。Pedro v3 未改善该 workload，目前没有安全修复方案。 |
| [`tmpfs-flistxattr-small-list/`](fs/tmpfs-flistxattr-small-list/) | 既有修复[`1e7cd8a53b72`](https://github.com/torvalds/linux/commit/1e7cd8a53b72)首次包含于v7.2主线正式版。我们7月17日回复验证当时已有的主线修复。不是我们编写的补丁被采纳。 | per-superblock cache commit 已消除所测 tmpfs small-list slowdown；已结题，证据保留。 |
| [`fsnotify-concurrent-inotify-watch-setup/`](fs/fsnotify-concurrent-inotify-watch-setup/) | Jan Kara认可竞争解释，也认可现有取舍。我们7月29日补充对照后未查到新回复；所查主线仍使用连接器链表锁，未发现已完成的对应修复。 | P1/P4 低于 signal gate；P6/P8 表现为 superblock connector list 上的并行可扩展性损失。上游目前接受该取舍，并提到可能顺带改善它的未完成 rhashtable 工作。 |
| [`btrfs-remap-writeback-inhibition-v2/`](fs/btrfs-remap-writeback-inhibition-v2/) | 不再只是for-next：[`67bd829a14b7`](https://github.com/torvalds/linux/commit/67bd829a14b7d71ffea28147b98dadf9ee684568)已在主线，保留我们的`Tested-by`。9月12日7.2 stable review也列明该主线提交；本轮未核实首次包含它的stable正式版本。 | 独立 patch 验证中，所含 4 KiB micro-workload 的 clone 成本降低约 `27%`、dedupe 降低约 `22%`。这不是 broad Btrfs 性能结论。 |
| [`apparmor-af-unix-send-old-abi-6456cc/`](security/apparmor-af-unix-send-old-abi-6456cc/) | John Johansen于8月6日回复；我们9月23日发送原型和补测后未查到新回复。所查主线尚无拟议的提前跳过unconfined路径检查。 | 精确源码增量使 unconfined AF_UNIX datagram `sendmmsg()` 慢 `15.295%`，独立大 runner 复现同方向。报告只询问如何在保留 ABI correctness 修复的同时降低 send-path 成本。 |
| [`io-uring-msg-ring-send-fd-install/`](io_uring/io-uring-msg-ring-send-fd-install/) | 本次快照的公开RFC最后一封回复为Gabriel的9月26日回复；所查主线没有对应批量／预填充方案。 | 原精确安装慢差仍为 `11.621%`。新 64/256 连接的稳定 TCP 总耗时变化不足 1%。旧专用 slab／32 与独立共享 kmalloc／128 均无明确收益，后者 64 项注册＋首填慢 `9.61%`。材料已[按测试分类](io_uring/io-uring-msg-ring-send-fd-install/bare-metal/README.zh-CN.md)，不稳定项及历史 v3 内存代价均保留。 |
| [`io-uring-futex-inflight-wait-wake/`](io_uring/io-uring-futex-inflight-wait-wake/) | 主线修复[`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef)保留我们的`Reported-by`；当前源码仍保留WAKE／private-WAIT的tracking修改。另收到stable队列与review邮件，正式版本包含情况另行核实。 | direct-parent A/B 仍为 `+9.268%`。patch 1 让较新 master 上的 private workload 快 `3.392%`；patch 2 对 private 中性，并让 matched shared workload 快 `1.578%`。同步 WAKE 上不必要的 tracking 已修复，因此除非正式版本验证失败，这条线已经收口。 |
| [`io-uring-futex-waitv-accounted-allocation/`](io_uring/io-uring-futex-waitv-accounted-allocation/) | 所查主线WAITV仍使用`GFP_KERNEL_ACCOUNT`。原accounted allocation补丁已合入，不等于这份独立性能报告已解决。 | 精确 direct-parent A/B 为 `+8.091%`，另一份 standalone 为 `+6.488%`，标量对照为 `-0.555%`。另一组 v7.2 诊断仍把约 `8–9%` 窄成本隔离到 accounted WAITV allocation，但不建议取消 memcg accounting。 |

## 限定成本报告与补丁跟进

2026-10-10核对上游状态；默认时间片一行另纳入10月13日已发送的第四版回复。

| 证据 | 报告状态 | 当前技术状态 |
| --- | --- | --- |
| [`kernfs-empty-cgroup-removal/`](fs/kernfs-empty-cgroup-removal/) | T.J. Mercier最新回复是9月16日；我们9月17日发送独立标记原型与补丁后未查到新回复，所查主线尚无该快捷路径。 | 原精确删除增幅约 46%～48%。新[独立请求标记](fs/kernfs-empty-cgroup-removal/bare-metal/inode-requested/README.zh-CN.md)在一轮矩阵中使删除耗时降低 22.36%～22.72%、连续整段降低 7.35%～9.01%，代价是每节点增加 8 字节。旧实验分开保留；正确性覆盖仍未齐，无应用级影响测量。 |
| [`fair默认时间片参数未变`](kernel/sched/sched-fair-default-slice-noop/) | 第四版补丁及结果于10月13日回复且公开归档；Peter当日认为实现合理，尚非正式Reviewed-by或合入通知。当天核验的主线／tip sched/core未采用优化。 | 第四版在已测组合中修正模式／uclamp两项边界，默认参数未变成本约616.51→162.39 ns/次；不是此前版本慢差的完整归因。 |

## 已准备的补充报告

| 证据 | 报告状态 | 核心证据 |
| --- | --- | --- |
| [`MMTests管道交接`](kernel/sched/sched-fair-delayed-requeue-handoff-333238da/) | 10月30日移入正式证据，邮件未发送。来自复现MMTests/will-it-scale，不计入模型生成场景的发现成果。 | 原生CORE、15-worker：v7.3线程／进程吞吐比v7.1.3低15.69%／14.82%；v7.2匹配诊断撤回`333238da9a19`后线程吞吐提高13.65%。撤回不是修复建议。 |

## 优化原型

| 材料 | 当前状态 | 核心证据 |
| --- | --- | --- |
| [`换出回退页序`](mm/mm-swap-fallback-folio-order/) | 限定于回收路径的顺序调整原型；其他后端的适用性仍待验证。 | 固定mm-stable配对，初始THP的PAGEOUT＋缓存再访问累计CPU减少14.84%／13.14%，每8MiB省565／521µs；非应用收益。 |

## 其他候选

另外四个性能回归／优化讨论候选放在 [`candidate/`](candidate/) 下。

| 候选 | 当前判断 | 核心证据 |
| --- | --- | --- |
| [`共享swap计数清理`](candidate/mm-swap-count-batch-cleanup/) | 同锁批量清理原型。 | 固定MM树配对，每16页munmap合并均值3.264→2.337µs，平均省0.927µs；收益随槽位位置变化、样本有重叠，未证明应用收益。 |
| [`lazyfree PTE批量失败`](candidate/mm-lazyfree-dirty-pte-batch-354dffd2/) | 局部优化有效，尚未证明应用收益。 | 同v7.2原型使PAGEOUT减少71.37%（60.69µs／32MiB）；全流程均值−1.53%／−2.26%，与漂移和波动相近。 |
| [`async cancel miss`](candidate/io-uring-async-cancel-miss-5623eb1e/) | 可发送，但范围限于 guaranteed-miss slow path 和 whole commit。 | 精确 A/B `+8.833%`，hit control `-0.562%`。 |
| [`region API ring lifecycle`](candidate/io-uring-region-api-ring-lifecycle/) | 材料已准备；现实影响限于反复创建、销毁短生命周期 ring。 | release `+10.832%`；两个相邻 direct-parent pair 为 `+4.563%`、`+3.019%`。 |

## 已有补丁的验证

状态核对于2026-10-10；既有验证数据不改。

- [RT sysctl 读取重建条件](kernel/sched/sched-rt-sysctl-read-rebuild/)：
  Joseph Salisbury 已有补丁仅适配 v7.2 上下文，所测 RT 读取循环耗时降低约 96.9%。
  成功写入仍执行维护，有限正确性检查通过。9月24日已反馈原线程。所查主线读取和失败写入
  后仍会重建。这是验证Joseph既有方案，不是我们新设计的修复。

## 如何阅读 bundle

每个目标目录才是该结论的权威来源。典型结构包括：

- `README.md` / `README.zh-CN.md`：范围、结果和限制；
- `bare-metal/`：紧凑的入选测量，以及源码/运行身份；
- `reproducer/`：standalone 或正式实验的精确 workload 源码；
- `upstream-status/`：有日期的排重、修复审计和线程状态（若存在）。

根索引不再重复整份实验叙述。精确方法和数字应以 target README 及其紧凑表格为准。

## 证据取舍

- 只保留整理后的摘要、standalone reproducer、紧凑 CSV/TSV/JSON 数据，以及理解 claim
  所需的小型 attribution probe。
- 直接写清 workload scope 和 caveat，不把窄 source-calibrated 结果包装成 generic
  subsystem claim。
