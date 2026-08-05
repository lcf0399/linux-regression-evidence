# 上游状态审计

审计日期：2026-08-05。

- 已核对精确引入提交及 AppArmor 6.17 merge 历史；
- 已检查 `security/apparmor/af_unix.c` 与
  `security/apparmor/include/af_unix.h` 至当前 Linus master 的后续历史，未发现
  明显移除本次所测路径或提供等效性能修复的后续改动；
- 按精确 commit、subject 和 AF_UNIX/AppArmor 发送性能关键词检索，未找到同类报告；
- 未识别出该精确 commit 的独立公开 patch 线程，因此准备的是一封窄范围新报告，
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
