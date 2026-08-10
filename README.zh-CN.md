# Linux 性能回归证据仓库

这个仓库保存经过整理的 Linux 性能回归证据，以及对应的上游 follow-up 和 patch
验证材料。

## 已发送或已进入上游线程

索引更新于 2026-08-10。可访问的 Gmail 线程最近一次集中核对仍为 2026-08-07；
mprotect 条目依据学生邮箱保存的原始邮件头和本地证据记录。邮件送达、维护者回复和
技术结论分开记录。

| 证据 | 上游状态 | 当前技术状态 |
| --- | --- | --- |
| [`mprotect-shared-dirty-toggle/`](mprotect-shared-dirty-toggle/) | 最新机制分解回复已于 2026-07-24 发送，`In-Reply-To` 与 `References` 完整；本地尚未记录更晚回复。 | 精确 A/B 将 `39.77%` slowdown 归因到 `cac1db8c3aad`。嵌套诊断从 generic single-PTE update/flush 处理和 normal page/folio lookup 合计回收 `87.29%` 缺口。Pedro v3 未改善该 workload，目前没有安全修复方案。 |
| [`tmpfs-flistxattr-small-list/`](tmpfs-flistxattr-small-list/) | Jan Kara 指出 `1e7cd8a53b72` 后，我们已按要求把精确裸机验证回复到原线程。 | per-superblock cache commit 已消除所测 tmpfs small-list slowdown；除非出现新的 post-fix case，这条线在技术上已经收口。 |
| [`fsnotify-concurrent-inotify-watch-setup/`](fsnotify-concurrent-inotify-watch-setup/) | Jan Kara 认可 P6/P8 的竞争解释并询问单 worker。P1/P4 回复已于 2026-07-29 发出；2026-08-07 核对 Gmail 时没有更晚回复。 | P1/P4 低于 signal gate；P6/P8 表现为 superblock connector list 上的并行可扩展性损失。上游目前接受该取舍，并提到可能顺带改善它的未完成 rhashtable 工作。 |
| [`btrfs-remap-writeback-inhibition-v2/`](btrfs-remap-writeback-inhibition-v2/) | David Sterba 已确认验证结果、在 patch 记录中加入证据链接，并把修正补丁加入 Btrfs `for-next`。 | 独立 patch 验证中，所含 4 KiB micro-workload 的 clone 成本降低约 `27%`、dedupe 降低约 `22%`。这不是 broad Btrfs 性能结论。 |
| [`apparmor-af-unix-send-old-abi-6456cc/`](apparmor-af-unix-send-old-abi-6456cc/) | 报告于 2026-08-05 发出。John Johansen 回复称上游会调查，并预计能改善当前回归，但不承诺完全恢复。 | 精确源码增量使 unconfined AF_UNIX datagram `sendmmsg()` 慢 `15.295%`，独立大 runner 复现同方向。报告只询问如何在保留 ABI correctness 修复的同时降低 send-path 成本。 |
| [`io-uring-msg-ring-send-fd-install/`](io-uring-msg-ring-send-fd-install/) | 报告于 2026-08-07 发出并进入 io-uring lore 公开归档；当天核对 Gmail 时尚无回复。 | 精确 A/B 将 `11.621%` fixed-file 安装 slowdown 归因到 `7029acd8a950`；64 至 4,096 槽位均为同方向。这是窄 registration/update 取舍。 |
| [`io-uring-futex-inflight-wait-wake/`](io-uring-futex-inflight-wait-wake/) | 报告和 patch 验证回复分别于 2026-07-30、2026-07-31 发出；Jens Axboe 给出两枚补丁。2026-08-07 核对 Gmail 时没有更晚回复。 | direct-parent A/B 仍为 `+9.268%`。在较新的冻结 master 上，patch 1 让 private workload 快 `3.392%`；patch 2 对 private 中性，并让 matched shared workload 快 `1.578%`。两组源码基线不相减。 |

## 尚未发送的候选

四项未发送材料统一放在 [`candidate/`](candidate/) 下。进入该目录只表示材料尚未提交
上游，不表示一定值得发送；其中 NOP 已按诊断接口结果收口。

| 候选 | 当前判断 | 核心证据 |
| --- | --- | --- |
| [`WAITV accounted allocation`](candidate/io-uring-futex-waitv-accounted-allocation/) | 当前最清晰的发送候选；发送前刷新排重和收件人。 | 单一 allocation flag 变化；精确 A/B `+8.091%`，standalone `+6.488%`，scalar control `-0.555%`。 |
| [`async cancel miss`](candidate/io-uring-async-cancel-miss-5623eb1e/) | 可发送，但范围限于 guaranteed-miss slow path 和 whole commit。 | 精确 A/B `+8.833%`，hit control `-0.562%`。 |
| [`region API ring lifecycle`](candidate/io-uring-region-api-ring-lifecycle/) | 材料已准备；现实影响限于反复创建、销毁短生命周期 ring。 | release `+10.832%`；两个相邻 direct-parent pair 为 `+4.563%`、`+3.019%`。 |
| [`NOP diagnostic control`](candidate/io-uring-nop-diagnostic-control/) | 不建议发性能回归报告；保留为诊断接口成本证据。 | release plain/inject `+15.801%/+16.240%`；精确提交成分 `+8.311%/+8.692%`。 |

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
