# Linux 性能回归证据仓库

这个仓库保存经过整理的 Linux 性能回归证据，以及对应的上游 follow-up 和 patch
验证材料。

## 已发送或已进入上游线程

状态核对于2026-10-10：检查Gmail（包括垃圾邮件与已删除）、精确公开线程，以及主线
[`3857c2fe5449`／v7.3-rc6](https://github.com/torvalds/linux/tree/3857c2fe5449541e24afc5efdb0f81a8a8f9a3a0)相关源码。
本次范围是下列11个已有本机裸机证据的已发送问题主题；早期没有这类验证的学校邮箱报告暂不纳入。
mprotect已实时核对公开线程，但未连接学生邮箱收件箱。
3类已有对应主线修复；fsnotify接受维护者的取舍后结题，另7类尚无确认解决。
不等于3枚由我们编写的补丁被合入。2026-10-10已将tmpfs、Btrfs、标量futex和fsnotify
四条线全部结题，不再默认跟进或追加实验，证据继续保留。
tmpfs修复在6月15日已进入主线，早于7月6日报告；当时被测7.1正式版仍不含它。
另已核实它首次包含于8月17日发布的v7.2主线正式版，未核实更旧stable分支的回移情况。
以下日期使用北京时间。本轮未重跑性能；
“未发现新回复”仅限上述已查来源，不能保证没有私下或尚未索引的讨论。

I/O 源文件主动探索阶段已经结束，目前没有排队的新 target 或裸机实验。本仓库后续只
跟踪既有上游线程，以及由用户逐项决定是否发送的候选材料。

| 证据 | 上游状态 | 当前技术状态 |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mprotect-shared-dirty-toggle/) | [公开线程](https://lore-kernel.gnuweeb.org/regressions/7c9eeb21.1c04f.19efaacbb2f.Coremail.chengfenglin%40stu.xmu.edu.cn/t/)仍以我们7月24日的补充结束；未找到更晚公开回复或对应修复。 | 精确 A/B 将 `39.77%` slowdown 归因到 `cac1db8c3aad`。嵌套诊断从 generic single-PTE update/flush 处理和 normal page/folio lookup 合计回收 `87.29%` 缺口。Pedro v3 未改善该 workload，目前没有安全修复方案。 |
| [`tmpfs-flistxattr-small-list/`](tmpfs-flistxattr-small-list/) | 既有修复[`1e7cd8a53b72`](https://github.com/torvalds/linux/commit/1e7cd8a53b72)首次包含于v7.2主线正式版。我们7月17日回复验证当时已有的主线修复；未查到之后的新回复。不是我们编写的补丁被采纳。 | per-superblock cache commit 已消除所测 tmpfs small-list slowdown；已结题，证据保留。 |
| [`fsnotify-concurrent-inotify-watch-setup/`](fsnotify-concurrent-inotify-watch-setup/) | Jan Kara认可竞争解释，也认可现有取舍。我们7月29日补充对照后未查到新回复；所查主线仍使用连接器链表锁，未发现已完成的对应修复。 | P1/P4 低于 signal gate；P6/P8 表现为 superblock connector list 上的并行可扩展性损失。上游目前接受该取舍，并提到可能顺带改善它的未完成 rhashtable 工作。 |
| [`btrfs-remap-writeback-inhibition-v2/`](btrfs-remap-writeback-inhibition-v2/) | 不再只是for-next：[`67bd829a14b7`](https://github.com/torvalds/linux/commit/67bd829a14b7d71ffea28147b98dadf9ee684568)已在主线，保留我们的`Tested-by`。9月12日7.2 stable review也列明该主线提交；本轮未核实首次包含它的stable正式版本。 | 独立 patch 验证中，所含 4 KiB micro-workload 的 clone 成本降低约 `27%`、dedupe 降低约 `22%`。这不是 broad Btrfs 性能结论。 |
| [`apparmor-af-unix-send-old-abi-6456cc/`](apparmor-af-unix-send-old-abi-6456cc/) | John Johansen于8月6日回复；我们9月23日发送原型和补测后未查到新回复。所查主线尚无拟议的提前跳过unconfined路径检查。 | 精确源码增量使 unconfined AF_UNIX datagram `sendmmsg()` 慢 `15.295%`，独立大 runner 复现同方向。报告只询问如何在保留 ABI correctness 修复的同时降低 send-path 成本。 |
| [`io-uring-msg-ring-send-fd-install/`](io-uring-msg-ring-send-fd-install/) | 新结果已于10月1日发给Uzair，不再是未发草稿；未查到之后的新回复。公开RFC最新仍为Gabriel的9月26日回复；所查主线没有对应批量／预填充方案。 | 原精确安装慢差仍为 `11.621%`。新 64/256 连接的稳定 TCP 总耗时变化不足 1%。旧专用 slab／32 与独立共享 kmalloc／128 均无明确收益，后者 64 项注册＋首填慢 `9.61%`。材料已[按测试分类](io-uring-msg-ring-send-fd-install/bare-metal/README.zh-CN.md)，不稳定项及历史 v3 内存代价均保留。 |
| [`io-uring-futex-inflight-wait-wake/`](io-uring-futex-inflight-wait-wake/) | 主线修复[`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef)保留我们的`Reported-by`；当前源码仍保留WAKE／private-WAIT的tracking修改。另收到stable队列与review邮件，正式版本包含情况另行核实。 | direct-parent A/B 仍为 `+9.268%`。patch 1 让较新 master 上的 private workload 快 `3.392%`；patch 2 对 private 中性，并让 matched shared workload 快 `1.578%`。同步 WAKE 上不必要的 tracking 已修复，因此除非正式版本验证失败，这条线已经收口。 |
| [`io-uring-futex-waitv-accounted-allocation/`](io-uring-futex-waitv-accounted-allocation/) | 我们8月31日回复后未查到回应；主线WAITV仍使用`GFP_KERNEL_ACCOUNT`。原accounted allocation补丁已合入，不等于这份独立性能报告已解决。 | 精确 direct-parent A/B 为 `+8.091%`，另一份 standalone 为 `+6.488%`，标量对照为 `-0.555%`。另一组 v7.2 诊断仍把约 `8–9%` 窄成本隔离到 accounted WAITV allocation，但不建议取消 memcg accounting。 |

## 限定成本报告与补丁跟进

2026-10-10核对上游状态；默认时间片一行另纳入10月13日已发送的第四版回复。

| 证据 | 报告状态 | 当前技术状态 |
| --- | --- | --- |
| [`kernfs-empty-cgroup-removal/`](kernfs-empty-cgroup-removal/) | T.J. Mercier最新回复是9月16日；我们9月17日发送独立标记原型与补丁后未查到新回复，所查主线尚无该快捷路径。 | 原精确删除增幅约 46%～48%。新[独立请求标记](kernfs-empty-cgroup-removal/bare-metal/inode-requested/README.zh-CN.md)在一轮矩阵中使删除耗时降低 22.36%～22.72%、连续整段降低 7.35%～9.01%，代价是每节点增加 8 字节。旧实验分开保留；正确性覆盖仍未齐，无应用级影响测量。 |
| [`fair默认时间片参数未变`](sched-fair-default-slice-noop/) | RFC于10月9日发送，10月10日补发原样补丁附件；10月12日Peter回复。实测第四版补丁及结果已于10月13日回复；这是已发送状态，不代表已被接受或合入。 | 第四版在已测组合中修正模式／uclamp两项边界，默认参数未变成本约616.51→162.39 ns/次；不是此前版本慢差的完整归因。 |

## 尚未发送的候选与已关闭诊断项

四个未发送报告／优化讨论候选和一个未发送但已关闭的诊断 bundle 统一放在
[`candidate/`](candidate/) 下。进入该目录不会形成新的实验队列，也不表示一定值得
发送上游。

| 候选 | 当前判断 | 核心证据 |
| --- | --- | --- |
| [`lazyfree PTE批量失败`](candidate/mm-lazyfree-dirty-pte-batch-354dffd2/) | 保留的优化讨论候选，暂不发送；未证明应用收益。 | 同v7.2原型使PAGEOUT减少71.37%（60.69µs／32MiB）；全流程均值−1.53%／−2.26%，与漂移和波动相近。 |
| [`fair 同步交接`](candidate/sched-fair-delayed-requeue-handoff-333238da/) | 讨论稿，未发送；诊断性撤回不是修复。 | 撤回 `333238da9a19` 后，无／轻计算完整往返缩短 11.31%／9.58%；加入第三个忙线程后没有稳定的明显优势。 |
| [`async cancel miss`](candidate/io-uring-async-cancel-miss-5623eb1e/) | 可发送，但范围限于 guaranteed-miss slow path 和 whole commit。 | 精确 A/B `+8.833%`，hit control `-0.562%`。 |
| [`region API ring lifecycle`](candidate/io-uring-region-api-ring-lifecycle/) | 材料已准备；现实影响限于反复创建、销毁短生命周期 ring。 | release `+10.832%`；两个相邻 direct-parent pair 为 `+4.563%`、`+3.019%`。 |
| [`NOP diagnostic control`](candidate/io-uring-nop-diagnostic-control/) | 不建议发性能回归报告；保留为诊断接口成本证据。 | release plain/inject `+15.801%/+16.240%`；精确提交成分 `+8.311%/+8.692%`。 |

## 已有补丁的验证

状态核对于2026-10-10；既有验证数据不改。

- [RT sysctl 读取重建条件](sched-rt-sysctl-read-rebuild/)：
  Joseph Salisbury 已有补丁仅适配 v7.2 上下文，所测 RT 读取循环耗时降低约 96.9%。
  成功写入仍执行维护，有限正确性检查通过。9月24日已反馈原线程，Oracle地址退信后又单独
  补送Joseph的kernel.org地址。未查到技术回复，休假自动回复不计入；当前主线读取和失败写入
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
- 不公开私有草稿、失败 scratch log、庞大 raw runner workspace、可重建产物或本地归档。
- 上游邮件引用本仓库时使用不可变 commit 链接。
- 直接写清 workload scope 和 caveat，不把窄 source-calibrated 结果包装成 generic
  subsystem claim。
