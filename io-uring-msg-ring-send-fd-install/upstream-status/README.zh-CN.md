# 上游状态审计

引入变更由 Jens Axboe 以
[`[PATCH 06/14] io_uring/rsrc: get rid of per-ring io_rsrc_node list`](https://lore.kernel.org/io-uring/20241029152249.667290-7-axboe@kernel.dk/)
发出，随后合入为 `7029acd8a950`。该补丁有意把 resource-node 分配移到 registration
阶段，以取消 per-ring serialization 并避免资源回收停滞。本证据测量的正是这项很窄的
registration/update trade-off，不建议回退该修复。

后续
[`ed9f3112a8a8`](https://github.com/torvalds/linux/commit/ed9f3112a8a8f6e6919d3b9da2651fa302df7be3)
因频繁 alloc/free 成本较高而恢复了 resource node 与 mapped buffer cache。该提交已经包含
在 Linux 7.1 和 7.1.3 中，而 matched Linux 6.12.95/7.1.3 对照仍测到 F0
`+15.602%`，所以后来的 cache 没有消除本 workload 的 release 信号。

2026-08-06 对 Linus master `fcaeecb8b0cd` 的更新审计显示，
`io_msg_install_complete()` 仍经 `__io_fixed_fd_install()` 到达
`io_rsrc_node_alloc()`，同时 node cache 也仍存在。我们没有在 master 上重新计时，因此
这只是一条源码拓扑结论。以引入提交、opcode 和分配路径定向检索公开 io-uring
邮件归档，未检索到相同 MSG_RING SEND_FD 批量空槽安装 workload 的既有报告。

建议发送一封窄 scope 的新回归报告，并在正文中链接原 patch 06 线程。精确 Message-ID
和源码引用见 [`refs.tsv`](refs.tsv)。
