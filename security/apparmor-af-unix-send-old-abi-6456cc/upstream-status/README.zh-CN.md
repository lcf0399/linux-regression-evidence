# 上游状态审计

## 2026-10-19：维护者表示会调查，跟踪已关闭

regzbot于10月13日以微基准问题、开发者研究为由将本报告标为inconclusive并停止跟踪，
不是确认修复。[跟踪记录](../../../upstream-status/README.zh-CN.md)。

John Johansen的[8月6日回复（北京时间）](https://lists.ubuntu.com/archives/apparmor/2026-August/014884.html)
表示要调查开销来源，并不保证完全恢复；这能证明维护者回应和调查意向，不能证明已完成分析。
[9月23日的原型与测量](https://lists.openwall.net/linux-kernel/2026/09/22/2475)来自Chengfeng，
不能算作John的修复。本次核对的公开线程没有显示维护者对该原型给出后续技术结论。

## 2026-08-05至08-07历史快照

源码/修复审计日期：2026-08-05。邮件状态更新：2026-08-07。

回归报告已于 2026-08-05 发出。John Johansen 回复称 AppArmor 维护者会调查
unconfined 路径额外开销的来源；他指出相关提交看起来已经有 early bailout，并认为
当前回归应当还能改善，但不承诺完全恢复。

- 已核对精确引入提交及 AppArmor 6.17 merge 历史；
- 已检查 `security/apparmor/af_unix.c` 与
  `security/apparmor/include/af_unix.h` 至当前 Linus master 的后续历史，未发现
  明显移除本次所测路径或提供等效性能修复的后续改动；
- 按精确 commit、subject 和 AF_UNIX/AppArmor 发送性能关键词检索，未找到同类报告；
- 未识别出该精确 commit 的独立公开 patch 线程，因此报告以窄范围新邮件发出，
  不是回复旧线程。

当前 Linus master `0d8395707651` 的 `scripts/get_maintainer.pl` 对两个改动文件
给出的路由：

- To：John Johansen `<john.johansen@canonical.com>`、Georgia Garcia
  `<georgia.garcia@canonical.com>`；
- Cc：Paul Moore `<paul@paul-moore.com>`、James Morris
  `<jmorris@namei.org>`、Serge E. Hallyn `<serge@hallyn.com>`、
  `apparmor@lists.ubuntu.com`、`linux-security-module@vger.kernel.org`、
  `linux-kernel@vger.kernel.org`、`regressions@lists.linux.dev`。

这项审计不表示已在 current master 重测相同百分比；精确 A/B 仍只绑定本 bundle
描述的隔离源码增量。
