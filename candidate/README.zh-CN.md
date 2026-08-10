# 尚未发送的 Linux 性能回归候选

本目录集中保存尚未提交上游的整理后证据。进入本目录只表示“尚未发送”，不代表已经决定
报告，也不改变各 bundle 自己的结论边界。

| bundle | 当前处置 | 主要边界 |
| --- | --- | --- |
| [`io-uring-futex-waitv-accounted-allocation/`](io-uring-futex-waitv-accounted-allocation/) | 当前最清晰的发送候选 | 保留 memcg accounting 语义，只询问能否降低逐 WAITV 分配成本。 |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | 可发送 | 只覆盖 guaranteed-miss async cancel；归因停在 whole commit。 |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | 已准备，发送前需刷新上游审计 | 只覆盖反复创建和销毁短生命周期标准 ring。 |
| [`io-uring-nop-diagnostic-control/`](io-uring-nop-diagnostic-control/) | 已收口，不建议发送 | NOP 是测试/控制 opcode，不代表应用 I/O。 |

决定发送某项时，再执行当前上游排重、修复审计、`get_maintainer.pl` 收件人核对和公开文件
边界检查。邮件草稿保留在本地并由 `.gitignore` 排除。
