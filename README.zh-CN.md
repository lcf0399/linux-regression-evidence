# Linux 性能回归证据仓库

这个仓库保存经过整理的 Linux 性能回归证据，以及对应的上游 follow-up 和 patch
验证材料。

下述上游状态为标明日期的快照，相关来源链接见各场景材料。

[2026-10-19公开跟踪汇总](upstream-status/README.zh-CN.md)已补入5条人工关闭记录，
并区分实际修复、接受取舍、inconclusive及未关闭的旧条目；人工关闭不增加修复数量。

## 近期更新（2026-11-07）

- 三份MMTests补充报告分别归入调度和x86目录：管道交接、Redis安全刷新后代价、sembench均衡取舍。
  它们不是模型生成方法的发现，不因新增目录而重复计数。
- 管道交接已补DELAY_ZERO对照及上游公平性解释；Redis已补微码和精确提交CPU剖析；
  sembench保留v4仅小幅改善的结果。收到回复、确认修复和合入分别记录。
- zswap仍为未发送候选；新增八worker复测保留有限额收益及恢复／无限额路径的噪声和不利方向。
  普通NOP及time-unmap保留已确认的局部成本，不声称解释全部版本差距。
- 当前新实验以7.3为最新端；已完成的7.2材料不改版本身份。
  libMicro一轮取得1,092份有效结果，33个条件仅为最新端慢差线索，不能计作33个已确认回归；
  原生网络矩阵仍在运行。完整探索档案、未验收结果和执行队列不在本公开仓库。

## 按子系统查找

15份证据材料按内核子系统归类。

| 目录 | 场景数 | 内容 |
| --- | ---: | --- |
| [fs/](fs/) | 4 | Btrfs、fsnotify、kernfs、tmpfs xattr |
| [mm/](mm/) | 2 | mprotect；换出回退页序 |
| [io_uring/](io_uring/) | 3 | MSG_RING SEND_FD、标量 futex、WAITV |
| [kernel/sched/](kernel/sched/) | 4 | fair 默认时间片、延迟重新入队管道交接、sembench空闲均衡取舍、RT sysctl |
| [security/](security/) | 1 | AppArmor AF_UNIX |
| [arch/x86/](arch/x86/) | 1 | Redis安全刷新后的持续代价 |

[candidate/](candidate/)保存另外7份独立候选材料及一份补充审阅清单。
tmpfs归入文件系统、AppArmor归入安全子系统；不是仅按benchmark调用的接口或研究目录命名分类。

## 已发送或已进入上游线程

以下源码/合入审计保留2026-10-10快照；regzbot跟踪状态另于2026-10-19更新。主线源码参考为
[`3857c2fe5449`／v7.3-rc6](https://github.com/torvalds/linux/tree/3857c2fe5449541e24afc5efdb0f81a8a8f9a3a0)相关源码。
下列11个已报告问题主题均有裸机证据。
3类已有对应主线修复；fsnotify接受维护者的取舍后结题。其他报告不能因跟踪器关闭而记为已修复，
也不能概括为除最近投稿外都已有上游最终技术结论。
不等于3枚由我们编写的补丁被合入。2026-10-10已将tmpfs、Btrfs、标量futex和fsnotify
四条线全部结题，不再默认跟进或追加实验，证据继续保留。
tmpfs修复在6月15日已进入主线，早于7月6日报告；当时被测7.1正式版仍不含它。
另已核实它首次包含于8月17日发布的v7.2主线正式版，未核实更旧stable分支的回移情况。
以下日期使用北京时间。

| 证据 | 上游状态 | 当前技术状态 |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mm/mprotect-shared-dirty-toggle/) | regzbot于10月12日以inconclusive关闭：其他人未能复现、可能是微基准问题。[跟踪详情](upstream-status/README.zh-CN.md)。不是已确认修复。 | 精确 A/B 将 `39.77%` slowdown 归因到 `cac1db8c3aad`。嵌套诊断从 generic single-PTE update/flush 处理和 normal page/folio lookup 合计回收 `87.29%` 缺口。Pedro v3 未改善该 workload，目前没有安全修复方案。 |
| [`tmpfs-flistxattr-small-list/`](fs/tmpfs-flistxattr-small-list/) | 既有修复[`1e7cd8a53b72`](https://github.com/torvalds/linux/commit/1e7cd8a53b72)首次包含于v7.2主线正式版。我们7月17日回复验证当时已有的主线修复。不是我们编写的补丁被采纳。 | per-superblock cache commit 已消除所测 tmpfs small-list slowdown；已结题，证据保留。 |
| [`fsnotify-concurrent-inotify-watch-setup/`](fs/fsnotify-concurrent-inotify-watch-setup/) | regzbot于10月12日关闭，理由是Jan Kara接受该可扩展性取舍并计划改善；不是补丁修复。[详情](fs/fsnotify-concurrent-inotify-watch-setup/upstream-status/README.zh-CN.md)。 | P1/P4 低于 signal gate；P6/P8 表现为 superblock connector list 上的并行可扩展性损失。上游目前接受该取舍，并提到可能顺带改善它的未完成 rhashtable 工作。 |
| [`btrfs-remap-writeback-inhibition-v2/`](fs/btrfs-remap-writeback-inhibition-v2/) | 不再只是for-next：[`67bd829a14b7`](https://github.com/torvalds/linux/commit/67bd829a14b7d71ffea28147b98dadf9ee684568)已在主线，保留我们的`Tested-by`。9月12日7.2 stable review也列明该主线提交；本轮未核实首次包含它的stable正式版本。 | 独立 patch 验证中，所含 4 KiB micro-workload 的 clone 成本降低约 `27%`、dedupe 降低约 `22%`。这不是 broad Btrfs 性能结论。 |
| [`apparmor-af-unix-send-old-abi-6456cc/`](security/apparmor-af-unix-send-old-abi-6456cc/) | regzbot于10月13日以微基准问题、开发者研究为由inconclusive关闭。John的公开回复是承诺调查；9月原型来自本研究，未确认维护者后续结论。[详情](security/apparmor-af-unix-send-old-abi-6456cc/upstream-status/README.zh-CN.md)。 | 精确源码增量使 unconfined AF_UNIX datagram `sendmmsg()` 慢 `15.295%`，独立大 runner 复现同方向。报告只询问如何在保留 ABI correctness 修复的同时降低 send-path 成本。 |
| [`io-uring-msg-ring-send-fd-install/`](io_uring/io-uring-msg-ring-send-fd-install/) | regzbot于10月13日以微基准问题、已有开发者研究为由inconclusive关闭。Uzair的三枚RFC及Gabriel评审属实，缓存内存/现实用途取舍尚未获认可；不是修复合入。[详情](io_uring/io-uring-msg-ring-send-fd-install/upstream-status/README.zh-CN.md)。 | 原精确安装慢差仍为 `11.621%`。新 64/256 连接的稳定 TCP 总耗时变化不足 1%。旧专用 slab／32 与独立共享 kmalloc／128 均无明确收益，后者 64 项注册＋首填慢 `9.61%`。材料已[按测试分类](io_uring/io-uring-msg-ring-send-fd-install/bare-metal/README.zh-CN.md)，不稳定项及历史 v3 内存代价均保留。 |
| [`io-uring-futex-inflight-wait-wake/`](io_uring/io-uring-futex-inflight-wait-wake/) | 主线修复[`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef)保留我们的`Reported-by`；regzbot关联它及`4d327bbd1cd2`，标示v7.3-rc1和稳定分支回补。[记录](io_uring/io-uring-futex-inflight-wait-wake/upstream-status/README.zh-CN.md)。这不是inconclusive关闭。 | direct-parent A/B 仍为 `+9.268%`。patch 1 让较新 master 上的 private workload 快 `3.392%`；patch 2 对 private 中性，并让 matched shared workload 快 `1.578%`。同步 WAKE 上不必要的 tracking 已修复，因此除非正式版本验证失败，这条线已经收口。 |
| [`io-uring-futex-waitv-accounted-allocation/`](io_uring/io-uring-futex-waitv-accounted-allocation/) | regzbot于10月13日因缺少反馈、很可能属于微基准问题而inconclusive关闭；不是性能修复。[详情](io_uring/io-uring-futex-waitv-accounted-allocation/upstream-status/README.zh-CN.md)。原accounting补丁合入不等于成本报告已解决。 | 精确 direct-parent A/B 为 `+8.091%`，另一份 standalone 为 `+6.488%`，标量对照为 `-0.555%`。另一组 v7.2 诊断仍把约 `8–9%` 窄成本隔离到 accounted WAITV allocation，但不建议取消 memcg accounting。 |

## 限定成本报告与补丁跟进

此组上游核验起于2026-10-10；默认时间片另纳入10月13日回复及10月19日正式投稿。
各项合入判断保留其核验日期，不代表本次重新核对了全部维护树。

| 证据 | 报告状态 | 当前技术状态 |
| --- | --- | --- |
| [`kernfs-empty-cgroup-removal/`](fs/kernfs-empty-cgroup-removal/) | T.J. Mercier最新回复是9月16日；我们9月17日发送独立标记原型与补丁后未查到新回复，所查主线尚无该快捷路径。 | 原精确删除增幅约 46%～48%。新[独立请求标记](fs/kernfs-empty-cgroup-removal/bare-metal/inode-requested/README.zh-CN.md)在一轮矩阵中使删除耗时降低 22.36%～22.72%、连续整段降低 7.35%～9.01%，代价是每节点增加 8 字节。旧实验分开保留；正确性覆盖仍未齐，无应用级影响测量。 |
| [`fair默认时间片参数未变`](kernel/sched/sched-fair-default-slice-noop/) | Peter对10月13日第四版回复认为实现合理；同代码的[正式补丁](https://lists.openwall.net/linux-kernel/2026/10/18/950)已于10月19日（北京时间）投稿，有公开归档记录。合理性认可不是Reviewed-by，投稿也不代表合入；本次未刷新合入核验。 | 第四版在已测组合中修正模式／uclamp两项边界，默认参数未变成本约616.51→162.39 ns/次；不是此前版本慢差的完整归因。 |

## 已发送的补充报告

| 证据 | 报告状态 | 核心证据 |
| --- | --- | --- |
| [`MMTests管道交接`](kernel/sched/sched-fair-delayed-requeue-handoff-333238da/) | 10月30日报告，11月3日回复DELAY_ZERO对照；11月7日收到公平性解释，当前暂停追加探索。来源是MMTests/will-it-scale，不是模型生成场景。 | 15-worker下7.3线程／进程吞吐比7.1.3低15.69%／14.82%；同镜像关闭DELAY_ZERO提高13.70%，但不是已验证公平的正式修复。 |
| [`Redis安全刷新后代价`](arch/x86/redis-ibpb-post-flush-a3af84b0/) | 11月3日原报告、11月4日CPU剖析和微码回复已发送，进一步有界探索收束。精确公开归档地址待核验。来源是MMTests原生Redis。 | 7.3 ZPOPMIN吞吐−12.87%，完整测试时间+0.58%；两微码均重现，新增CPU样本分散在内核收发路径，硬件持续机制未解释清楚。 |
| [`sembench空闲均衡取舍`](kernel/sched/sched-newidle-sembench-tradeoff/) | 用户于11月5日确认已发送，作为已发布Bresenham v4的测试反馈；实际发送头及公开归档尚未独立核验。来自MMTests原生sembench，不计入模型生成场景的发现成果。 | 7.3关闭NI_RANDOM后815-worker均值+23.48%；匹配v4仅+1.98%，未恢复主要慢差。开关仅作归因控制，不建议普遍关闭策略。 |

## 优化原型

| 材料 | 当前状态 | 核心证据 |
| --- | --- | --- |
| [`换出回退页序`](mm/mm-swap-fallback-folio-order/) | 限定于回收路径的顺序调整原型；其他后端的适用性仍待验证。 | 固定mm-stable配对，初始THP的PAGEOUT＋缓存再访问累计CPU减少14.84%／13.14%，每8MiB省565／521µs；非应用收益。 |

## 其他候选

另外七份性能回归／优化讨论候选材料放在 [`candidate/`](candidate/) 下。
[补充审阅清单](candidate/additional-review/README.zh-CN.md)汇集22条审阅线索，
区分目标版本已测、历史结果待复核和证据不足，不将条目数当成独立缺陷数。

| 候选 | 当前判断 | 核心证据 |
| --- | --- | --- |
| [`普通NOP的混合CQE支持成本`](candidate/io-uring-mixed-cqe-nop-e26dca67/) | 公共路径精确归因与中英文报告完成，未发送。 | 原生20线程在e26dca67fde1子版吞吐下降5.564%，父版回切−0.0324%；只报告这一明确来源，不将全部跨版本差额归给它。 |
| [`COLD去活跃化取舍`](candidate/mm-madv-cold-deactivation-tradeoff/) | 已有优化的状态相关成本与收益。 | 精确提交下，每16MiB新填充文件页CPU多51µs／13.20%，最老代文件页省247µs／68.30%；应用影响未知，不建议直接撤回优化。 |
| [`有限zswap限额统计刷新`](candidate/mm-zswap-limit-rstat-flush/) | 已有问题的精确归因与计数方案验证。 | 精确提交使单worker程序CPU多15.56%；独立MM配对派生版减少21.78%。无限额单worker未检出明确代价，并发及生产取舍未定。 |
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

本地邮件材料统一分类：`email/`只放正文、译稿、完整邮件与附件；`mail-tools/`放准备／核验／发送工具；
来信与实际投递核验放`private-provenance/mail/`。这些本地目录不参与公开导出。
确认发送成功后清理临时发送状态，不长期保留防重发标记；结果仍不确定的尝试先核对，不自动重发。

## 证据取舍

- 只保留整理后的摘要、standalone reproducer、紧凑 CSV/TSV/JSON 数据，以及理解 claim
  所需的小型 attribution probe。
- 直接写清 workload scope 和 caveat，不把窄 source-calibrated 结果包装成 generic
  subsystem claim。
