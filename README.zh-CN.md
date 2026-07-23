# Linux 回归证据仓库

这个仓库保存经过整理的 Linux 性能回归证据，以及对应的上游 follow-up 或 patch 验证。

## 上游状态

状态核对时间为 2026-07-23。表中将邮件是否送达、维护者是否回复和技术结论分开记录。

| 证据 | 上游线程状态 | 当前技术状态 |
| --- | --- | --- |
| `mprotect-shared-dirty-toggle/` | 上游讨论仍在进行；维护者询问 Pedro v3 是否有效，并讨论了 `vm_normal_folio()` 的成本。当前公开线程尚未包含后续精确机制分解。 | matched 测试表明 Pedro v3 没有改善这条 workload；精确诊断将 `cac1db8c3aad` 缺口的大部分归因到 generic single-PTE update/flush 路径和 normal-path folio lookup，目前没有提出修复。 |
| `tmpfs-flistxattr-small-list/` | Jan Kara 已回复并指出 `1e7cd8a53b72`；我们已按要求把精确裸机验证回复到原线程，之后尚无新回复。 | per-superblock cache commit 已消除这条窄 workload 中测得的 slowdown；除非出现新的 post-fix 场景，这条线在技术上已经收口。 |
| `fsnotify-concurrent-inotify-watch-setup/` | 报告已经发送并被公开 regression tracker 跟踪，目前尚无回复。 | exact A/B 与诊断证据仍有效；目前没有上游修复，也没有维护者对该 trade-off 的明确结论。 |
| `btrfs-remap-writeback-inhibition-v2/` | David Sterba 已确认收到测试报告，将证据链接加入 patch 记录，并把修正后的补丁加入 Btrfs `for-next`。 | 独立结果支持 v2 在所测 4 KiB clone/dedupe workload 上的改进；这是 patch 验证，不是 broad Btrfs 性能结论。 |

## 当前证据

- `mprotect-shared-dirty-toggle/`

  一条很窄的 Linux MM `mprotect()` workload：在 shared-dirty 4 KiB base-page
  mapping 上做权限修改。bare-metal 结果先把 slowdown 缩小到 `v6.16 -> v6.17`
  release window；后续精确 direct-parent/child 夹心把测得信号归因到
  `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`：child 相对 parent
  中点慢 `39.77%`，而 parent 漂移只有 `0.87%`。

  当前口径：这是 source-calibrated shared-dirty PTE workload，不是 generic
  `mprotect()` regression claim。

- `tmpfs-flistxattr-small-list/`

  一条很窄的 Linux FS `flistxattr(fd)` workload：tmpfs 文件上有少量 `user.*`
  xattr。围绕 `52b364fed6e1 shmem: adapt to rhashtable-based simple_xattrs
  with lazy allocation` 的 bare-metal parent/child A/B 显示，tmpfs 从旧 rbtree
  path 切到 lazy rhashtable-based `simple_xattrs` 后，小列表固定成本明显增加。

  后续精确 parent/child 验证表明，`1e7cd8a53b72 ("simpe_xattr: use per-sb
  cache")` 已消除测得的 slowdown：child 相对直接 parent 快约 `35.9%`，相对
  Linux 7.0.14 控制中值快约 `4.2%`。

  当前口径：这是 tmpfs small-list `flistxattr(fd)` 回归，不是 generic xattr 或
  generic tmpfs regression claim。

- `fsnotify-concurrent-inotify-watch-setup/`

  精确三启动 parent/child/parent A/B 将 P8 distinct-inode inotify watch 增删
  slowdown 归因到 `94bd01253c3d fsnotify: Track inode connectors for a
  superblock`；child 在 absolute 与 matched paired 指标上慢约 `15.1%` 至
  `21.6%`。第二组精确 scaling 夹心把首个已测稳定材料性点放在 P6（`9.0%` 至
  `10.9%`），同窗口 P8 信号增强到 `16.8%` 至 `19.6%`，而 P1/P4 仍低于 signal gate。
  同 commit probe 将成本收窄为 per-superblock list mutation 或其额外持锁时间，以及
  lock handoff。独立的 stock `inotifywait` trace 又表明，8 个递归 watcher 监控真实 Linux
  源码树时会形成相同的多进程、distinct-inode connector 拓扑。

  2026-07-20 的源码与 prior-art 审计覆盖 `v7.1.4`、`v7.2-rc4`、Linus tip、
  linux-next 和 linux-fs 维护者分支；目标锁/链表操作仍在，未找到等效修复或既有回归报告。
  该审计不表示已在最新 tip 上重新测得完全相同的 A/B 百分比。

  当前口径：性能 claim 只限精确 commit 前后的并发 distinct-inode watch 增删；真实
  软件 trace 只是拓扑 gate，不是应用 timing，也不建议回退 sparse-unmount 优化。

- `btrfs-remap-writeback-inhibition-v2/`

  对上游 v2 补丁的独立裸机验证；该补丁将 transaction 内的 writeback-inhibition
  xarray 替换为 fixed inline buffer。在 matched control/patch/control 夹心实验中，
  本目录所含 4 KiB Btrfs micro-workload 的 `FICLONERANGE` 均值下降约 `27.0%`，
  `FIDEDUPERANGE` 均值下降约 `22.0%`。

  当前口径：这是 brd-backed Btrfs 上的窄 4 KiB clone/dedupe micro-workload，
  不是 generic remap-range 或真实应用性能 claim。

## 证据取舍

- 只保留整理后的 README、standalone reproducer、紧凑 CSV/TSV/JSON summary，以及理解
  claim 所需的小型 attribution probe。
- 不上传私有邮件草稿、失败 scratch logs、庞大的 raw runner workspace 或 local-only
  archive。
- 给上游邮件引用时，优先使用固定 commit 链接。
- 直接写清 workload scope 和 caveat；不要把窄 source-calibrated workload 包装成
  generic subsystem regression。
