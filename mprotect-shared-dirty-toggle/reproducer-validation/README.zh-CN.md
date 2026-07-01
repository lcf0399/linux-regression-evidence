# standalone reproducer lab 验证

这个目录总结 `mprotect_shared_dirty_reproducer.c` standalone workload 的 lab
验证运行。

这类运行的目的比前面的 formal framework runs 更窄：确认给维护者检查用的更小
reproducer 在 lab QEMU 环境里仍然保持同样的 timing 方向。

## SMP lab follow-up

本轮 validation run 使用支持 SMP 的 x86/QEMU direct-boot 配置：

- `CONFIG_SMP=y`
- `CONFIG_NR_CPUS=16`
- `CONFIG_ACPI=y`
- `CONFIG_ACPI_PROCESSOR=y`
- guest cmdline 不带 `noapic`

follow-up matrix 使用同一份 standalone reproducer 和同样的 interleaved clean
performance 设置：

- host label: `lcf`
- QEMU: direct boot
- kernels: `v6.12.77`、`v6.19.9`、`akpm/mm mm-unstable 444fc9435e57`
- guest CPUs: `QEMU_SMP=1/2/4/8/16`
- guest memory: 1/2/4 CPU 为 `14336 MiB`，8 CPU 为 `16384 MiB`，16 CPU 为
  `32768 MiB`
- repetitions: `5`
- order: interleaved
- coverage: disabled
- extra guest cmdline: `tsc=unstable clocksource=refined-jiffies`
- workload external rounds: `5`

serial-log validation 从 run reports 解析 serial log 路径，并检查 guest 实际枚举
到的 CPU 数：

- `1/2/4/8 CPU`：每档检查 15 个 serial logs，`cpu_mismatches=0`，
  `noapic_logs=0`
- `16 CPU`：检查 14 个 serial logs，`cpu_mismatches=0`，`noapic_logs=0`

16 CPU 行作为 extended follow-up 保留。该行 v6.12.77 有一次 QEMU failure，
因此更适合作为 supporting evidence，而不是最干净的 primary row。

我又针对 16 CPU 行单独重跑了一次。该 rerun 已干净完成：检查了 15 个 serial
logs，全部匹配 `QEMU_SMP=16`，没有 log 含 `noapic`，三棵 kernel 都没有 failed
run。该 rerun 的 `iteration_ns_per_page` 为：v6.12.77 `386.8`，v6.19.9
`607.2`，mm-unstable `575.0`。这说明前一轮 v6.12.77 QEMU failure 更像是偶发
问题；但 16 CPU 结果仍作为 extended row，因为它使用更大的 32 GiB guest memory，
而且比 1/2/4/8 CPU 行更容易受噪声影响。

主要指标是 `iteration_ns_per_page`，越低越好。

| Guest CPUs | Guest memory | v6.12.77 | v6.19.9 | mm-unstable | mm-unstable vs v6.19 | v6.12 -> v6.19 gap closed |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 14336 MiB | 296.4 | 548.6 | 498.6 | 9.1% faster | 19.8% |
| 2 | 14336 MiB | 327.2 | 564.8 | 488.4 | 13.5% faster | 32.2% |
| 4 | 14336 MiB | 319.8 | 578.2 | 505.8 | 12.5% faster | 28.0% |
| 8 | 16384 MiB | 336.4 | 570.4 | 508.2 | 10.9% faster | 26.6% |
| 16 | 32768 MiB | 380.0 | 624.0 | 553.8 | 11.3% faster | 28.8% |

这轮 SMP follow-up 说明，在 guest 确实枚举到多 CPU 的情况下，standalone
reproducer 仍保持同样的大方向：`v6.19.9` 慢于 `v6.12.77`，当前
`mm-unstable` 有改善，但没有回到 `v6.12.77` 水平。

分阶段字段显示，主要 gap 出现在 `mprotect(PROT_READ)` 和
`mprotect(PROT_READ | PROT_WRITE)` 两个阶段。完整 per-metric 表见
`lab-smp-summary-20260526.csv`。

## caveat

这些 validation runs 是针对更小 reproducer 的 5 次重复 screening run，不替代前面的
formal evidence。QEMU guest run 报告 `expected_match_ratio=100`、
`unexpected_results=0`，但 minimal guest 环境没有提供和单独 state audit 一样的
smaps state-shape 可见性。state-shape 结论仍以 `../state-audit-summary/` 为准。

## 文件

- `lab-smp-iteration-comparison-20260526.csv`：精简 SMP follow-up 对比表。
- `lab-smp-summary-20260526.csv`：SMP follow-up 的 per-version/per-metric summary。
- `lab-smp-summary-20260526.json`：同一份 SMP follow-up 数据，JSON 格式。
- `lab-smp-serial-check-20260526.json`：SMP follow-up 的 serial-log guest CPU
  validation。
- `lab-smp-16cpu-rerun-iteration-comparison-20260526.csv`：16 CPU 定向干净重跑的
  对比表。
- `lab-smp-16cpu-rerun-summary-20260526.csv`：16 CPU 定向干净重跑的
  per-version/per-metric summary。
- `lab-smp-16cpu-rerun-summary-20260526.json`：同一份 16 CPU 定向重跑数据，
  包含 serial-log validation。
- `profile/`：runner 使用的 generated workload profile。
- `../reproducer/`：这轮验证使用的 canonical standalone source。
- `run-env/`：lab 行的运行环境、执行顺序、完成哨兵和 metadata。
