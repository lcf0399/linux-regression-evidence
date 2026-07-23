# v7.1.3 shared single-PTE hint fast path：双门禁 A/B

更新时间：2026-07-22 UTC

## 结论

这轮实验把 `vm_normal_page()`/folio lookup 的归因推进成了一个可执行候选，但反向门禁
否决了该候选作为修复：

- 对原报告中的 64 MiB shared-dirty、4 KiB base-page workload，候选相对两侧
  v7.1.3 baseline 中点快 `17.36%`；
- 配对运行时 trace 恰好少了 `2048` 次 `vm_normal_page()` 和 `2048` 次
  `mprotect_folio_pte_batch()`，与两次 1024-PTE protect/restore walk 对应；
- 对 2 MiB PTE-mapped large-folio workload，候选反而慢 `65.80%`。

因此，shared base-page 热路径中的 page/folio lookup 确实是可测开销，但
`pte_batch_hint() == 1` 不是 x86 上安全的 base-page 判据。当前补丁冻结为定位探针，
不能作为上游修复建议。

## 候选与构建身份

两个内核来自同一份正式 v7.1.3 源码快照
`199c9959d3a9b53f346c221757fc7ac507fbac50`；其中 `mm/mprotect.c` 与
Pedro v3 系列终点 `89e613bc0b2d6d4a18a09b161131ce4ca5c70f2a` 完全相同：

| 角色 | kernel release | 修改 |
| --- | --- | --- |
| baseline | `7.1.3-mprotect-pv3-full-89e613bc0b2d` | 未修改 v7.1.3 |
| candidate | `7.1.3-mprotect-hint-one-000000000000` | 只应用本目录候选补丁 |

候选只在非 NUMA、`VM_SHARED` 且 `pte_batch_hint() == 1` 时跳过
`vm_normal_page()`，并令 `nr_ptes=1`。补丁 SHA-256 为
`5189a4a3547504573b84246b2617e58eb48558f79a316f91f128198f01cfb452`。

两个内核的 canonical config SHA-256 都是
`b1484511b7b7a3e3b1b8187018c2886ef939aa80e230be0aa3d7b74a202c3376`，使用
GCC 15.2.0、相同 Kbuild 元数据、签名密钥和 `preempt=none`。机器码审计确认
`change_pte_range()` 确实不同。

## 4 KiB base-page 正向门禁

顺序为 `baseline A -> candidate -> baseline B`，每点 fresh boot；每点 3 次 warm-up、
15 次正式测量，每次 1000 cycles。主指标 `iteration_ns_per_page` 包含
protect、restore 和 write-touch，越低越好。

| 点位 | n | 均值 ns/page | CV | 语义失败 |
| --- | ---: | ---: | ---: | ---: |
| baseline A | 15 | 56.933 | 0.804% | 0 |
| candidate | 15 | 47.133 | 1.096% | 0 |
| baseline B | 15 | 57.133 | 0.904% | 0 |

candidate 相对 baseline 中点为 `-17.358%`，baseline 漂移只有 `+0.351%`；删除
每点首轮后分别为 `-17.323%` 和 `+0.376%`。45 行测量全部通过返回值和
4 KiB/no-THP 状态检查。

非计时 bpftrace smoke 的配对计数为：

| 角色 | `change_pte_range` | `vm_normal_page` | `mprotect_folio_pte_batch` |
| --- | ---: | ---: | ---: |
| baseline | 12 | 10996 | 2064 |
| candidate | 12 | 8948 | 16 |
| baseline - candidate | 0 | 2048 | 2048 |

进程装载和读取 `/proc/self/smaps` 也会产生少量共同调用，所以门禁使用配对差值，
而不是错误地要求 candidate 的全进程计数为零。

## PTE-mapped large-folio 反向门禁

reproducer 在 shmem 上创建 2 MiB shared mapping，先折叠为 PMD-mapped THP，再拆成
4 KiB PTE 映射并在计时前 fault-in。每轮状态检查均证明：

- `KernelPageSize` 和 `MMUPageSize` 都是 4 KiB，且 `ShmemPmdMapped=0`；
- 512 个 PTE 全部 present；
- PFN 对应 1 个 compound head、511 个 compound tail，512 页都带 `KPF_THP`。

这说明测试的不是普通 base pages，而是 512 个 PTE 仍指向同一个 2 MiB large folio。
主指标 `mprotect_ns_per_page` 只合计 protect 和 restore，以隔离权限修改路径。

| 点位 | n | protect | restore | 合计 ns/page | CV | 语义失败 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| baseline A | 15 | 12.000 | 11.000 | 23.000 | 0.000% | 0 |
| candidate | 15 | 19.133 | 19.000 | 38.133 | 0.923% | 0 |
| baseline B | 15 | 12.000 | 11.000 | 23.000 | 0.000% | 0 |

candidate 相对 baseline 中点为 `+65.797%`，baseline 漂移为 `0%`；drop-first 仍为
`+65.839%`。反向门禁明确失败。

## 机制解释与下一步边界

在普通 x86 base-page 场景中，候选省掉逐 PTE 的 page/folio lookup，因而恢复约
17% 性能；但 x86 的 `pte_batch_hint()` 对 PTE-mapped large folio 同样返回 1，候选
误把它当成普通页逐个处理，丢失原本的 folio batching，因而大幅变慢。

这不是“定位失败”，而是排除了一个看似直接但不安全的修复方向。后续若继续设计修复，
必须找到不会丢失 PTE-mapped large-folio batching 的廉价判据或重构路径，并同时通过本目录
的 base-page 正向门禁和 large-folio 反向门禁。

## 证据入口

- `summary.tsv`、`sensitivity.tsv`、`decision.tsv`：base-page 汇总与决策；
- `runtime-trace-summary.tsv`：配对函数命中；
- `large-folio-summary.tsv`、`large-folio-sensitivity.tsv`、
  `large-folio-decision.tsv`：反向门禁；
- `runs/`、`large-folio-runs/`：正式原始测量；
- `manifests/`、`build-logs/`、`install-logs/`：源码与构建身份；
- `workload/`、`mprotect_shared_pte_mapped_thp_reproducer.c`：两个 reproducer；
- `terminal-state.tsv`：实验机最终恢复状态；
- `experiment-plan.zh-CN.md`：预先定义的问题、矩阵和判定规则。

所有正式点结束后，实验机已回到 `7.0.0-27-generic`；终态无 `BootNext`、无临时
UEFI 实验项、无 failed systemd unit，CPU 与 THP 配置均已恢复。
