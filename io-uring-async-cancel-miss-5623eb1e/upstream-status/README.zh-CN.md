# 上游状态审计

审计日期：2026-07-28。

Jens Axboe 以
[`[PATCH v2] io_uring/tctx: add separate lock for list of tctx's in ctx`](https://lore.kernel.org/io-uring/24a9e751-7442-4036-9b9f-8c144918c201@kernel.dk/)
发出引入变更，随后合入为 `5623eb1ed035`。review 讨论了异步取消路径为何必须在取得可
睡眠锁之前恢复 `TASK_RUNNING`，没有报告或测量本性能影响。

该提交通过给 `ctx->tctx_list` 增加独立保护，解除 `ctx->uring_lock` 与
`tctx->io_uring_lock` 的锁依赖。因此这里把 measured slowdown 写成整个 correctness
提交的一项窄 trade-off，不要求回退，也不把它描述成 mutex-only 诊断。

截至审计日，对 Linus master `62cc90241548`、Axboe `for-7.3/io_uring`
`c905736a` 和 Axboe `for-next` 的源码检查仍看到由 `tctx_lock` 保护的异步取消慢路径
遍历；没有找到对 guaranteed key miss 删除或绕开它的后续变更。我们没有在这些树上重新
计时，因此这只是一条源码拓扑结论。

公开 io-uring 归档中，以精确提交及 async cancel miss/slowdown/performance 为关键词，
没有找到与本 workload 相同的报告。因此建议回复原 v2 线程，并明确保持 whole-commit
范围。Message-ID 与源码引用见 [`refs.tsv`](refs.tsv)。
