# Linux 回归证据仓库

这个仓库保存经过整理的 Linux 性能回归证据，以及对应的上游 follow-up 或 patch 验证。

## 上游状态

索引更新于 2026-08-06。各条目的审计日期分别记录；既有线程状态并未全部重新审计。
表中将邮件是否送达、维护者是否回复和技术结论分开记录。

| 证据 | 上游线程状态 | 当前技术状态 |
| --- | --- | --- |
| `mprotect-shared-dirty-toggle/` | 上游讨论仍在进行；维护者询问 Pedro v3 是否有效，并讨论了 `vm_normal_folio()` 的成本。当前公开线程尚未包含后续精确机制分解。 | matched 测试表明 Pedro v3 没有改善这条 workload。对精确 `cac1db8c3aad` child 的诊断将大部分缺口归因到 generic single-PTE update/flush 处理和 `vm_normal_folio()` lookup。独立的 v7.1.3 安全门禁测试后来的 `vm_normal_page()` 加 `page_folio()` 路径，其百分比不计入精确提交缺口分解。目前没有提出修复。 |
| `tmpfs-flistxattr-small-list/` | Jan Kara 已回复并指出 `1e7cd8a53b72`；我们已按要求把精确裸机验证回复到原线程，之后尚无新回复。 | per-superblock cache commit 已消除这条窄 workload 中测得的 slowdown；除非出现新的 post-fix 场景，这条线在技术上已经收口。 |
| `fsnotify-concurrent-inotify-watch-setup/` | 报告已经发送并被公开 regression tracker 跟踪，目前尚无回复。 | exact A/B 与诊断证据仍有效；目前没有上游修复，也没有维护者对该 trade-off 的明确结论。 |
| `btrfs-remap-writeback-inhibition-v2/` | David Sterba 已确认收到测试报告，将证据链接加入 patch 记录，并把修正后的补丁加入 Btrfs `for-next`。 | 独立结果支持 v2 在所测 4 KiB clone/dedupe workload 上的改进；这是 patch 验证，不是 broad Btrfs 性能结论。 |
| `io-uring-msg-ring-send-fd-install/` | 尚未发送报告。已经确定原始引入补丁线程和当前维护者；回复草稿仅保留本地并被 Git 忽略。 | 精确 direct-parent A/B 将 `11.621%` fixed-file 安装 slowdown 归因到 `7029acd8a950`；parent 漂移为 `0.097%`，各点实际抢占模式均为 `full`。64 至 4,096 槽位的适用范围检查在每个规模都得到同方向信号。这是很窄的 registration-time trade-off，不是普通 io_uring read/write 性能结论。 |
| `apparmor-af-unix-send-old-abi-6456cc/` | 尚未发送报告。2026-08-05 已核对精确提交、后续路径历史和公开归档；没有找到相同性能报告或明显的等效修复。 | standalone 精确源码差分夹心中，AF_UNIX datagram `sendmmsg()` 慢 `15.295%`；独立大 runner 又复现 direct `sendmmsg()` `+14.841%` 和 io_uring SEND `+17.953%`。报告只询问能否在保留旧 AppArmor policy ABI correctness 修复的同时降低 unconfined send-path 成本，不要求回退。 |
| `io-uring-futex-inflight-wait-wake/` | 原始报告已发送。Jens Axboe 回复说这项 tracking 对该操作偏重、同步 WAKE 不需要它，并给出两枚补丁。本地补丁验证已完成，结果回复尚未发送。 | direct-parent 结果仍为 `+9.268%`。在较新冻结 master 上，补丁 1 让 private workload 快 `3.392%`；补丁 2 对 private 基本中性，并让配对 shared workload 再快 `1.578%`。两组源码基线的百分比不相减。 |

## 当前证据

- `mprotect-shared-dirty-toggle/`

  一条很窄的 Linux MM `mprotect()` workload：在 shared-dirty 4 KiB base-page
  mapping 上做权限修改。bare-metal 结果先把 slowdown 缩小到 `v6.16 -> v6.17`
  release window；后续精确 direct-parent/child 夹心把测得信号归因到
  `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`：child 相对 parent
  中点慢 `39.77%`，而 parent 漂移只有 `0.87%`。

  精确机制分解使用该 direct child，其中 `change_pte_range()` 调用
  `vm_normal_folio()`。独立的 v7.1.3 诊断检查后来的 `vm_normal_page()` 再
  `page_folio()` 路径；其中 base-page 快 `17.36%` 和 large-folio 反向慢
  `65.80%` 只作为当前代码佐证与安全门禁，不参与精确提交百分比计算。

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

- `apparmor-af-unix-send-old-abi-6456cc/`

  一份不依赖 liburing 的 AF_UNIX datagram standalone 通过 socketpair 调用
  `sendmmsg()`，peer drain 与 payload 校验均放在计时区外。围绕
  `6456ccbd2ff7` 精确源码差分的 fresh-boot parent/child/parent 夹心中，child
  相对 parent 中点慢 `15.295%`；drop-first 为 `15.289%`、parent 漂移
  `-0.036%`，45 行 measured 数据全部通过。该主实验各点实际均为
  `preempt=none`。

  独立的大 runner 在各点实际均为 `preempt=full` 时复现同方向：direct
  `sendmmsg()` 慢 `14.841%`，io_uring SEND 慢 `17.953%`。两组结果分别分析。
  普通系统调用也复现信号，因此当前 claim 是 AppArmor AF_UNIX send path，
  不是 `io_uring/net.c`。

  当前口径：结论限于精确源码差分前后的 unconfined AF_UNIX datagram send。
  引入提交修复真实的旧 AppArmor policy ABI correctness 问题，所以只询问能否降低
  成本，不建议回退。

- `io-uring-msg-ring-send-fd-install/`

  一条很窄的 io_uring fixed-file registration/update workload：通过
  `IORING_MSG_SEND_FD`，以每批 64 个操作填满 4,096 个 target-table 空槽。围绕
  `7029acd8a950 ("io_uring/rsrc: get rid of per-ring io_rsrc_node list")` 的
  fresh-boot 精确 parent/child/parent 夹心中，child 相对 parent 中点慢 `11.621%`；
  删除首个 measured round 后为 `11.649%`，parent 漂移仅 `0.097%`，所有比较点
  实际运行模式均为 `preempt=full`。

  非计时 trace 确认，在精确提交处，固定文件安装内部嵌套的
  `io_rsrc_node_alloc()` 逐安装调用由 0 次变为 128 次。这是 direct-hit 证明，不表示
  一个 allocator 函数解释了全部 delta。后续 node cache 已包含在 Linux 7.1.3 中；
  独立 matched release 对照仍得到同方向信号。

  同一组精确内核上的 64、256、1,024 和 4,096 target-slot 检查中，child 在每个规模
  都慢 `8.399%` 至 `11.889%`。较小点噪声更高，因此它只作为适用范围补证，不替代
  正式的 4,096 槽位主结果。

  当前口径：这是 synthetic MSG_RING SEND_FD registration/update，不是应用
  benchmark，也不声称普通 io_uring read/write fast path 存在回归。引入补丁还解决了
  serialization 与资源回收停滞，因此不建议回退它。

- `io-uring-futex-inflight-wait-wake/`

  一条很窄的标量 io_uring futex workload：每个计时 cycle 先提交 32 个 private 标量
  wait，再提交 32 个标量 wake，并验证恰好 64 个 CQE、wait 返回 0、wake 返回 1。

  围绕 `079afb081c42` 的 fresh-boot direct-parent 夹心中，未改动正式源码相对 parent
  中点慢 `9.268%`，drop-first `9.269%`、parent 漂移 `-0.124%`、最大 CV
  `0.169%`；另一份 338 行 standalone 复现为 `10.020%`。90 行标量计时数据全部通过
  语义检查，各点实际运行模式均为 `preempt=full`。匹配的 `WAITV -> WAKE` profile 只变化
  `1.385%`，未过信号门。

  Jens Axboe 随后给出两枚补丁。在冻结 master `48a5a7ab8d6a` 上，补丁 1 相对
  baseline 控制让原 private workload 快 `3.392%`；补丁 2 对该 private 形状基本
  中性，并让严格配对的标量 shared-futex workload 相对 patch-1 控制再快 `1.578%`。
  private/shared 两轮合计 120 行正式计时全部通过，所有 measured boot 实际均为
  `preempt=full`。shared WAITV 未测试。这组结果使用较新的源码基线，不能从
  direct-parent 的 `9.268%` 中相减。

  当前口径：引入提交修复退出时 private-futex 生命周期/UAF 问题，证据不建议 revert，
  也不作 generic futex/io_uring claim；只询问是否能以更低逐请求成本保留同样保证。

## 证据取舍

- 只保留整理后的 README、standalone reproducer、紧凑 CSV/TSV/JSON summary，以及理解
  claim 所需的小型 attribution probe。
- 不上传私有邮件草稿、失败 scratch logs、庞大的 raw runner workspace 或 local-only
  archive。
- 给上游邮件引用时，优先使用固定 commit 链接。
- 直接写清 workload scope 和 caveat；不要把窄 source-calibrated workload 包装成
  generic subsystem regression。
