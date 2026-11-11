# Linux 性能回归／优化候选

本目录保存六份独立的性能回归／优化讨论候选材料，以及一份
[补充审阅清单](additional-review/README.zh-CN.md)。清单汇集22条审阅记录，
分别标明7.2验证、历史版本待复核和证据限制；不是22个已确认的独立缺陷。
各材料分别说明测量结果、机制和剩余限制，已有候选及已发送报告不重复收录。

[sembench空闲均衡报告](../kernel/sched/sched-newidle-sembench-tradeoff/)在用户于11月5日确认发送后，
已移入调度子系统目录，不再列为未发送候选。
[zswap补充](../mm/mm-zswap-limit-rstat-flush/)在11月11日发送且由Gmail Sent独立核验后，
已移入MM子系统目录。

| bundle | 当前处置 | 主要边界 |
| --- | --- | --- |
| [`io-uring-mixed-cqe-nop-e26dca67/`](io-uring-mixed-cqe-nop-e26dca67/) | 公共完成路径精确归因及中英文稿完成，未发送 | MMTests原生20线程普通NOP，混合CQE支持提交使吞吐下降5.564%，回切−0.0324%；不是全部约20%跨版本差距的解释，也不代表实际存储性能。 |
| [`mm-madv-cold-deactivation-tradeoff/`](mm-madv-cold-deactivation-tradeoff/) | 精确提交的状态相关取舍 | 每16MiB新填充文件页CPU多51µs／13.20%，最老代文件页省247µs／68.30%；不是应用整体退化，也不建议直接撤回优化。 |
| [`mm-swap-count-batch-cleanup/`](mm-swap-count-batch-cleanup/) | 同锁批量清理原型 | 固定 MM 树配对，扩展表保留时每16页 munmap 平均省0.927 µs；收益随位置变化，未证明应用收益。 |
| [`mm-lazyfree-dirty-pte-batch-354dffd2/`](mm-lazyfree-dirty-pte-batch-354dffd2/) | 局部优化有效，整体收益未明确 | PTE失败路径局部省约61µs；整体均值差与波动相近，应用自然命中未建立。 |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | 可发送 | 只覆盖 guaranteed-miss async cancel；归因停在 whole commit。 |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | 已测得生命周期成本增加 | 只覆盖反复创建和销毁短生命周期标准 ring。 |
