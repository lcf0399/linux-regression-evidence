# 尚未发送的 Linux 性能回归／优化候选

本目录集中保存六个尚未提交上游的报告／优化讨论候选。
进入本目录不代表已经决定报告，也不会形成新的实验队列；结论边界仍以各 bundle 为准。

| bundle | 当前处置 | 主要边界 |
| --- | --- | --- |
| [`mm-swap-count-batch-cleanup/`](mm-swap-count-batch-cleanup/) | F01 同锁批量清理 RFC 已准备待审，未发送 | 固定 MM 树配对，扩展表保留时每16页 munmap 平均省0.927 µs；收益随位置变化，未证明应用收益。 |
| [`mm-swap-fallback-folio-order/`](mm-swap-fallback-folio-order/) | 换出回退页序 RFC 已准备待审，未发送 | 固定 MM 树配对中，初始 THP 每 8 MiB 累计 CPU 省约 565／521 µs；非应用收益。 |
| [`mm-lazyfree-dirty-pte-batch-354dffd2/`](mm-lazyfree-dirty-pte-batch-354dffd2/) | 保留的优化讨论候选，暂不发送 | PTE失败路径局部省约61µs；整体均值差与波动相近，应用自然命中未建立。 |
| [`sched-fair-delayed-requeue-handoff-333238da/`](sched-fair-delayed-requeue-handoff-333238da/) | 讨论稿，未发送 | 同 CPU、轻计算同步交接；撤回只用于诊断，不是安全修复。 |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | 可发送 | 只覆盖 guaranteed-miss async cancel；归因停在 whole commit。 |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | 已准备，发送前需刷新上游审计 | 只覆盖反复创建和销毁短生命周期标准 ring。 |

决定发送某项时，再执行当前上游排重、修复审计、`get_maintainer.pl` 收件人核对和公开文件
边界检查。邮件草稿保留在本地并由 `.gitignore` 排除。
