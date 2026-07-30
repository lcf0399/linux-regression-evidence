# 上游状态审计

审计日期：2026-07-29。

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
应回复原 `[PATCH 2/2]` 线程。精确引用见 [`refs.tsv`](refs.tsv)。
