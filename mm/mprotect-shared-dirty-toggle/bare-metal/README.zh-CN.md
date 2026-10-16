# mprotect shared-dirty 真机证据

更新日期：2026-07-23 UTC

这里保存一条窄范围 workload 的紧凑真机证据：

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping；
- 提前写脏、无 THP backing 的 4 KiB base pages；
- 完整 range 的 `PROT_READ -> PROT_READ | PROT_WRITE -> write-touch`；
- 以越低越好的 `iteration_ns_per_page` 为主指标。

该结论不扩大为 generic `mprotect()` 或应用级回归。

## 结果

| 证据 | 结果 |
| --- | --- |
| `cac1db8c3aad` 精确 direct-parent/child A/B | child 相对 parent 中点慢 `39.77%`，parent 漂移 `0.87%` |
| 九点机制分解 | 在精确 child 上，parent-style 单 PTE update/flush 回收 `43.06%` 缺口；跳过 normal-path batch discovery 无可测影响；随后跳过 `vm_normal_folio()` 再回收 `44.47%`；合计回收 `87.29%` |
| Pedro v3 matched on/off | full v3 相对 no-v3 中点慢 `6.20%`，没有改善该 workload |
| v7.1.3 shared-PTE hint 诊断 | 4 KiB workload 快 `17.36%`，但经过验证的 PTE-mapped 2 MiB folio 慢 `65.80%`，因此当前代码候选被否决 |

各 points 表保留计算所用的全部 per-process 测量值；comparison 表保留中点、
漂移、drop-first 和 recovery 计算。

## 源码测点边界

精确 A/B 与九点分解使用 direct `45199f715b74 -> cac1db8c3aad` 源码转换。child 的
`change_pte_range()` 调用 `vm_normal_folio()`；所有精确缺口和 recovery 百分比均
属于这一转换。

Pedro v3 与 shared-PTE hint 比较使用后来的 v7.1.3 代码阶段，其中对应 lookup 先调用
`vm_normal_page()`，再调用 `page_folio()`。hint 诊断还会跳过 batch discovery，
所以 `17.36%` 不是 `vm_normal_page()` 的独立函数耗时。在较早的精确 child 源码
测点上，嵌套序列单独隔离出了可测的 `vm_normal_folio()` 成本，并表明当时的 batch
discovery 没有可测成本。v7.1.3 结果是当前代码佐证，不是隔离后的 lookup 测量；其
正向和反向门禁百分比不计入精确提交缺口分解。

## 测量契约

当前归因结果运行在 32 GiB 内存的 Intel Core i7-12700KF 真机上。每个点 fresh
boot，固定 P-core CPU 2，使用匹配的归一化配置、GCC 15.2.0 和 Kbuild 元数据，
governor/EPP 为 `performance`，关闭 Turbo，运行时 `preempt=none`。

base-page 点位使用 3 个外部 warm-up process 和 15 个 measured process；每个
measured process 在 10 次内部 warm-up 后执行 1,000 轮。large-folio 反向门禁使用
2 个外部 warm-up、15 个 measured process、200 轮和 5 次内部 warm-up。所有测量
都通过返回值与页形状门禁；`run-audit.tsv` 保留不同 boot ID 和 failed systemd
unit 为零的记录。

## 紧凑证据文件

| 文件 | 作用 |
| --- | --- |
| `exact-cac1-{points,components,comparison}.tsv` | 精确 direct-parent/child 结果 |
| `mechanism-{points,components,comparison}.tsv` | 九点精确 child 机制归因序列 |
| `pedro-v3-{points,components,comparison}.tsv` | matched no-v3/full-v3 结果 |
| `lookup-base-page-{points,comparison}.tsv` | v7.1.3 4 KiB page/folio lookup 正向门禁 |
| `lookup-large-folio-{points,comparison,shape}.tsv` | v7.1.3 PTE-mapped large-folio 反向门禁 |
| `lookup-trace.tsv` | 配对 function-entry 计数 |
| `source-identity.tsv` | source、代码状态、patch、config、compiler 和 release 身份 |
| `run-audit.tsv` | boot 身份、样本数、语义失败和 failed units |
| `patches/` | 归因实验实际使用的 4 份诊断 diff |
| `supporting/` | 紧凑的版本窗口、single-protect 和 base-page 状态检查 |

build log、objdump、安装日志、warm-up 输出、重复的逐点环境快照和 workload 副本
都是可重建中间产物，公开包不再保留。base-page 与 large-folio standalone
reproducer 位于 `../reproducer/`。

## 诊断 patch

这些 patch 是归因 probe，不是拟提交的修复：

| Patch | 诊断作用 |
| --- | --- |
| `0000-diagnostic-single-pte-parent-style-commit-fastpath.patch` | 保持 child 的 lookup/discovery，只恢复 parent-style 单 PTE update/flush |
| `0001-diagnostic-keep-folio-skip-batch-direct-single-pte.patch` | 保留 folio lookup，绕过 normal batch discovery |
| `0002-diagnostic-skip-folio-and-batch-direct-single-pte.patch` | 同时绕过 normal folio lookup 与 batch discovery |
| `0001-RFC-mm-mprotect-avoid-shared-folio-lookup-without-batch-hint.patch` | v7.1.3 正向与反向门禁使用的 `vm_normal_page()` 加 `page_folio()` 诊断 |
