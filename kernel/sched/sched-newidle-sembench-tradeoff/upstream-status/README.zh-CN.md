# 上游状态

更新：2026-11-05，北京时间。

- 用户已确认发送审阅定稿，无附件。定位为已发布Bresenham v4的测试反馈，以及
  new-idle策略取舍的负载补充；不是声称v4引入了此前的历史回归。
- 原线程：`[PATCH v4] sched/fair: Replace random newidle_balance with Bresenham accumulator`。
- 拟回复父邮件：`<20261030091946.2498870-1-wangqing7171@gmail.com>`；
  [原补丁](https://lkml.iu.edu/hypermail/linux/kernel/2610.3/14236.html)。
- 本地发信文件的Message-ID为
  `<179386941645.8.3608399700168338187.sembench-newidle-v4@gmail.com>`。
  这是准备文件中的身份，不冒充已发邮件头的独立回读。
- 本次整理未查询邮箱、核验公开归档或后续回复，没有新增“上游确认／修复／合入”结论。

场景已从candidate移到`kernel/sched/`。发现来源为MMTests原生sembench，
不计入模型生成场景的发现成果。定稿邮件留在本地，原始测量仍保留在原实验目录，
没有重发、复制整套原始数据或执行仓库发布。
