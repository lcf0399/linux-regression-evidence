# 裸机验证

公开主结果是三点 matched control 夹心：

```text
control A -> upstream v2 -> control B
```

每个点都在 `/dev/ram0` 上重建 Btrfs，运行 15 轮；每场景 10,000 次 4 KiB 操作，
固定 CPU 2 和 `performance` governor。control 与 v2 使用相同 Linux 7.1.0 源码
commit、normalized config、编译器、抢占合同和可复现 Kbuild metadata。

## Timing

| 口径 | 场景 | control 中点 ns/op | v2 ns/op | v2 delta |
| --- | --- | ---: | ---: | ---: |
| 全部轮次 | `FICLONERANGE` 4 KiB | 2957.118 | 2159.123 | **-26.986%** |
| 全部轮次 | `FIDEDUPERANGE` 4 KiB | 3541.199 | 2762.835 | **-21.980%** |
| 去掉首轮 | `FICLONERANGE` 4 KiB | 2961.755 | 2156.813 | **-27.178%** |
| 去掉首轮 | `FIDEDUPERANGE` 4 KiB | 3550.278 | 2764.666 | **-22.128%** |

逐点聚合见 `per-run-stats.tsv` 和 `control-sandwich.tsv`，启动/运行顺序及语义门禁见
`matrix-summary.tsv`。

## Direct hit

独立 direct-hit 在 v2 内核上执行 1,000 次 clone 和 1,000 次 dedupe。
`direct-hit.tsv` 记录 `btrfs_remap_file_range` 2,000 次、
`btrfs_inhibit_eb_writeback` 7,473 次、`btrfs_uninhibit_all_eb_writeback`
2,033 次；没有缺失必需 target，workload 的 unexpected result 为 0。

TSV 中的 `btrfs_inhibit_claim_slot` 是非必需 diagnostic target；该构建没有把它保留为可独立
trace 的函数，因此 0 次不属于 direct-hit failure。

## 身份

`source-identity.tsv` 记录精确源码、上游邮件、patch hash、config hash 和 kernel release；
`matched-build.tsv` 记录 control/v2 matched build 审计。本目录有意排除本机绝对构建路径和
可重建的完整日志。
