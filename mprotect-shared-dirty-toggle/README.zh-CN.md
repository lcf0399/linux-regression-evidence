# mprotect shared-dirty toggle 证据

这个目录支撑面向上游的限定报告：

```text
[REGRESSION] mm/mprotect: shared-dirty base-page toggle slower since v6.17
```

## 范围

workload 使用 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping，提前写脏 4 KiB
base pages，并反复执行：

```text
mprotect(PROT_READ)
mprotect(PROT_READ | PROT_WRITE)
write-touch
```

主指标是 `iteration_ns_per_page`，越低越好。该结果不声称存在 generic
`mprotect()` 或应用级回归。

## 当前结论

| 证据 | 结果 | 结论强度 |
| --- | --- | --- |
| [`cac1db8c3aad` 精确 parent/child A/B](bare-metal/20260721-cac1db8c3aad-exact-ab/) | child 相对 parent 中点慢 `39.77%`，parent 漂移 `0.87%` | 当前 commit-level culprit |
| [九点机制分解](bare-metal/20260722-cac1-folio-batch-decomposition-ab/) | generic single-PTE commit path 与 folio lookup 分别解释缺口的 `43.06%` 和 `44.47%`，合计 `87.29%` | attribution evidence |
| [Pedro v3 matched on/off](bare-metal/20260722-pedro-v3-exact-ab/) | full v3 相对 no-v3 中点慢 `6.20%` | 没有改善该 workload |
| [shared-PTE hint 双门禁](bare-metal/20260722-v713-shared-pte-hint-fastpath-ab/) | 4 KiB workload 快 `17.36%`，PTE-mapped 2 MiB folio 慢 `65.80%` | 候选已否决 |

精确归因运行在 i7-12700KF 真机上；每个点 fresh boot，固定 P-core CPU 2，匹配配置、
工具链和 Kbuild 元数据，governor/EPP 为 `performance`，Turbo 关闭，运行时
`preempt=none`。所有用于结论的 measured process 都通过返回值和页状态检查。

## 支持证据

[`bare-metal/supporting/`](bare-metal/supporting/) 保留三项仍有独立作用的真机结果：

- release window：`v6.16=25.000`、`v6.17=37.000 ns/page`；
- single-protect：`v6.16=8.000`、`v6.17=14.000 ns/page`；
- folio-order gate：9 轮均为 4 KiB order-0 pages，无 compound/THP。

这些结果用于版本窗口、调用形状和 state-shape 说明，不替代精确 commit-level A/B。

## 早期非真机背景

早期 QEMU/lab 筛选中，`v6.19.9` 在 1/2/4-vCPU 点上约为 `v6.12.77` 的
`1.63–1.67x`。当时 mm-unstable/Pedro v3 看起来有部分改善，但它不是 matched
patch-only A/B；后续真机 same-source on/off 得到 `+6.20%`，因此旧解释已经停用。

详细非真机材料只保留在本地实验归档中，不属于这个公开 evidence bundle。

## 目录

- [`bare-metal/`](bare-metal/)：当前真机结论与支持证据索引。
- [`reproducer/`](reproducer/)：standalone C reproducer 与辅助脚本。
- [`workload/`](workload/)：用于语义审计的用户态 workload source。

被精确 A/B 或九点分解取代、但仍有独特诊断信息的真机中间实验已移到本地归档，
不属于这个公开 evidence bundle。
