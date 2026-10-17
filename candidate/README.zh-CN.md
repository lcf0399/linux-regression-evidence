# Linux 性能回归／优化候选

本目录集中保存五个性能回归／优化讨论候选。
各材料分别说明测量结果、机制和剩余限制。

| bundle | 当前处置 | 主要边界 |
| --- | --- | --- |
| [`mm-swap-count-batch-cleanup/`](mm-swap-count-batch-cleanup/) | 同锁批量清理原型 | 固定 MM 树配对，扩展表保留时每16页 munmap 平均省0.927 µs；收益随位置变化，未证明应用收益。 |
| [`mm-lazyfree-dirty-pte-batch-354dffd2/`](mm-lazyfree-dirty-pte-batch-354dffd2/) | 局部优化有效，整体收益未明确 | PTE失败路径局部省约61µs；整体均值差与波动相近，应用自然命中未建立。 |
| [`sched-fair-delayed-requeue-handoff-333238da/`](sched-fair-delayed-requeue-handoff-333238da/) | 诊断性撤回，不是修复方案 | 同 CPU、轻计算同步交接；撤回只用于诊断，不是安全修复。 |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | 可发送 | 只覆盖 guaranteed-miss async cancel；归因停在 whole commit。 |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | 已测得生命周期成本增加 | 只覆盖反复创建和销毁短生命周期标准 ring。 |
