# 上游状态审计

源码/修复审计日期：2026-08-05。邮件状态更新：2026-08-07。

回归报告已于 2026-08-05 发出。John Johansen 回复称 AppArmor 维护者会调查
unconfined 路径额外开销的来源；他指出相关提交看起来已经有 early bailout，并认为
当前回归应当还能改善，但不承诺完全恢复。在其分析完成前无需继续追问。

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
