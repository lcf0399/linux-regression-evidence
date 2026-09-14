# 上游报告状态

2026-09-14 状态更新，依据作者发送确认及已保存线程：原报告 9 月 13 日已发送。
Tejun 询问现实影响，T.J. Mercier 随后提出 INODE_INITED 改动。
[补丁验证](../bare-metal/inode-inited/README.zh-CN.md)已完成，回复正在准备；
不声称修复已被接受、并发正确性已完整证明或已经合入。本次未重新搜索邮箱或公开归档。

原报告 Message-ID：
`<CANGjgdn=H50AaA-+O_GA-hkE9UwLANmkjx1zRsKUzxF5za8VfQ@mail.gmail.com>`。
补丁回复 Message-ID：
`<CABdmKX2Oer9wrxWdR8s48Czro8H51HY+S_mfnUj-RvhjCM1RgA@mail.gmail.com>`。

## 9 月 13 日源码审计（历史快照）

2026-09-13 重新核对了公开源码引用。下面的邮件检索沿用当天已保存的审计，
不是再次检查邮箱，也不是穷尽搜索。

原通知补丁为 T.J. Mercier 的 `[PATCH v5 2/3] kernfs: Send IN_DELETE_SELF and IN_IGNORED`，
Message-ID 为 `20260225223404.783173-3-tjmercier@google.com`。
其父提交增加删除期间的链接计数保护。公开 commit 说明原样保留为 `guard.commit`、
`notify.commit`；来源见 [refs.tsv](refs.tsv)。

两个修改都已合入，不是在讨论中的提案。它们于 2026-04-14 随
`driver-core-7.1-rc1`（合并提交 `4793dae01f47`）进入主线，正式 v7.1、v7.2 均已包含。
精确实验源码使用 7.0-rc3 开发基线；这个版本字符串不表示正式 v7.0 已包含它们。

源码审计固定到主线 `2f0c1cf72f4682178506f513bbf015e591b1aa4a`、
driver-core-next `d3bdf70cf0e862d7f1641522c32e55764cd5b481`、
driver-core-linus `77be3641f3e3a56e42a5ed889372ef395a933f3c`。
所选删除/链接计数函数仍与实测 notify 端一致；这不是整个内核或性能相同的证明。

相关后续工作分别处理：

- `d996995a0ffb86fa0ff0f953221892dced6143a1` 名称哈希简化已在固定主线。
  已证明命中该路径，新版比较快约 2%，但没有单独归因给这个提交。
- [9 月 5 日文件句柄解码修正](https://lists.openwall.net/linux-kernel/2026/09/05/828)：
  未包含在补测主线中。它给 `mount.c` 增加锁保护；本 workload 不调用 `open_by_handle_at()`。
- [9 月 10 日通知投递锁优化](https://lkml.iu.edu/2609.1/06430.html)：
  未包含在补测主线中。它优化内存压力下异步 `kernfs_notify_workfn()` 的持锁，
  与本次同步删除链不同；Ack 表示认可，不等于合入。
- [9 月 11 日 staged sysfs 注册 RFC](https://lkml.iu.edu/2609.1/11344.html)：
  未包含在补测主线中。它批量发布原本不可见的设备子树；本 workload 删除的是已经可见的 cgroup。

因此，固定 7.3-rc2 补测包含名称哈希修改，但没有叠加这三项提案。
它们均不是直接减少本场景逐节点删除工作的修复；本机没有测试它们，不能宣称计时绝对不受影响。

有限检索没有找到同一空叶子删除、同一精确增量的报告，不声称首次发现或穷尽讨论。
lore 部分页面不可读时使用镜像，不能把访问失败当作不存在报告。

原报告已经另开范围明确的 regression 线程，后续回复沿用原线程。
9 月 14 日补测明确把文件句柄入口读锁修复加入 A/B 两端作为前置条件，
没有单独测它的性能效果；不把上述历史审计中的“尚未补测”当成当前状态。
公开 evidence、补齐固定 commit URL 和真正发出回复是分开的步骤。
私人邮件文件由 `.gitignore` 排除，不进入公开证据。
