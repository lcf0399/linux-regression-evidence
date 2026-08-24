# 上游状态审计

历史源码/线程审计日期：2026-08-03；最新邮箱与 stable queue 状态核对日期：2026-08-24。

当前处置：上游已解决。提交
[`73e701909747`](https://github.com/torvalds/linux/commit/73e7019097473fc9f83a334ef2c6ab3343709fef)
取消同步 FUTEX_WAKE 的 inflight tracking，同时保留 WAIT 所需的生命周期处理，并带有
`Reported-by: Chengfeng Lin <lin2530632123@gmail.com>`。

Robert Morris 曾报告：进程仍有 private `IORING_OP_FUTEX_WAIT` pending 时被杀死，
可能触发 use-after-free。Jens Axboe 的修复把标量和向量 futex wait 标为 inflight，
让退出清理能在 `mm` 及 private futex hash 状态消失前找到并取消请求；该补丁合并为
`079afb081c42`。

这项 correctness 和生命周期要求必须保留。因此性能报告不能建议 revert，只能询问是否
存在成本更低但语义等价的 tracking 设计。

发送前源码审计核对了 Linus master
`fc02acf6ac0ccde0c805c2daa9148683cdd01ba8`，以及当日刷新到
`e5f87b066744` 的 io_uring `for-next`。两者的标量和向量 futex prep 都仍调用
`io_req_track_inflight()`。master 源码已直接核对；没有在这些最新分支上重新跑性能。

按精确 commit、futex wait/inflight 和 performance regression 检索公开 io-uring
归档，只找到原正确性报告与补丁线程，没有找到相同的性能报告或后续等效优化。因此邮件
已回复原 `[PATCH 2/2]` 线程。精确引用见 [`refs.tsv`](refs.tsv)。

Jens Axboe 随后回复：这套 inflight tracking 对该操作而言偏重，而且同步完成的 WAKE
一侧不需要它；并在报告线程中给出两枚补丁：

1. 把 tracking 从标量公共 prep 移到 WAIT 专用 prep，使 WAKE 不再被 tracking；
2. 只对包含 private 标量/向量 WAIT 的请求保留 tracking，因为 shared WAIT 不依赖每个
   `mm` 的 private futex hash 生命周期。

对原始两枚附件在冻结的 master `48a5a7ab8d6a` 上完成验证后，补丁 1 让原 private
WAIT/WAKE workload 快 `3.392%`，完整系列快 `3.416%`，完整系列相对补丁 1 只变化
`-0.025%`。这符合补丁 2 主要修改 shared WAIT 分支而非本轮 private 参数的预期。

随后用配对的标量 shared-futex 场景比较 patch 1 A、完整系列和 patch 1 B。完整系列
相对 patch 1 中点快 `1.578%`，控制漂移 `0.319%`，drop-first 仍为 `1.578%`。
这直接确认 patch 2 在标量 shared-WAIT 目标路径上的小幅改善；shared WAITV 仍未测试。

补丁验证结果回复已于 2026-07-31 从 Gmail 发送，Message-ID 为
`<CANGjgdkhQZWntcnpa2zBshGn_E7yaKDnPcSbH-HBBfXGWAw1+g@mail.gmail.com>`。
原始报告的 Message-ID 为
`<CANGjgdn=R_qyUdE=j9za+vkmqcxacbP-84OHXF4nZ4ho9qRyVg@mail.gmail.com>`。
原始报告与验证回复均在 Gmail `SENT` 中；验证回复的 `In-Reply-To`、`References` 和
实际收件人均正确。

2026-08-24，Greg Kroah-Hartman 针对该上游修复发出三封 stable queue 通知：

- 7.1：`<2026082416-chili-empower-39fb@gregkh>`；
- 6.18：`<2026082407-cruncher-carried-3088@gregkh>`；
- 7.2：`<2026082423-upstate-nature-93d0@gregkh>`。

这些通知证明修复已被选入三个 stable queue，但不能证明某个已经发布的 6.18.x、7.1.x 或
7.2.x 正式版本已经包含它。除非正式版本验证失败或上游要求补充证据，否则不再安排实验。
