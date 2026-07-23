# mprotect shared-dirty toggle 真机证据

更新时间：2026-07-23 UTC

这里保存限定 workload 的真机证据：

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping；
- 已写脏的 4 KiB base pages，无 THP；
- 整段 `PROT_READ -> PROT_READ | PROT_WRITE -> write-touch`；
- 主指标为 `iteration_ns_per_page`，越低越好。

该证据只支持 shared-dirty base-page PTE permission-change 场景，不代表 generic
`mprotect()` 或应用级回归。

## 当前核心证据

| 目录 | 证据角色 | 结论 |
| --- | --- | --- |
| [`20260721-cac1db8c3aad-exact-ab/`](20260721-cac1db8c3aad-exact-ab/) | exact direct-parent/child culprit A/B | `cac1db8c3aad` 相对 parent 中点慢 `39.77%`，parent 漂移 `0.87%` |
| [`20260722-cac1-folio-batch-decomposition-ab/`](20260722-cac1-folio-batch-decomposition-ab/) | 同提交机制分解 | generic single-PTE commit path 与 folio lookup 分别解释原始缺口的 `43.06%` 和 `44.47%`，合计 `87.29%` |
| [`20260722-pedro-v3-exact-ab/`](20260722-pedro-v3-exact-ab/) | matched fix validation | Pedro v3 相对 no-v3 中点慢 `6.20%`，没有改善该 workload |
| [`20260722-v713-shared-pte-hint-fastpath-ab/`](20260722-v713-shared-pte-hint-fastpath-ab/) | 候选路径与反向门禁 | 4 KiB workload 快 `17.36%`，但 PTE-mapped 2 MiB folio 慢 `65.80%`，候选被否决 |

精确 culprit A/B 与机制分解运行在 i7-12700KF 真机上。每个点 fresh boot，固定
P-core CPU 2，使用匹配的归一化配置、工具链和 Kbuild 元数据；governor/EPP 为
`performance`、Turbo 关闭，运行时 `preempt=none`。具体运行顺序、样本数、源码身份、
语义检查和原始测量以各目录内文件为准。

## 支持证据

[`supporting/`](supporting/) 保留三项仍有独立作用的早期真机结果：

| 目录 | 作用 | 主要结果 |
| --- | --- | --- |
| `supporting/20260623-narrow-6.16-6.19-3rounds/` | release-window narrowing | `v6.16=25.000`、`v6.17=37.000 ns/page` |
| `supporting/20260630-single-protect-followup/` | 排除 repeated-toggle 特有现象 | 单次 protect 为 `v6.16=8.000`、`v6.17=14.000 ns/page` |
| `supporting/20260706-folio-order-check/` | 页状态门禁 | 9 轮均为 4 KiB order-0 pages，无 compound/THP |

这三项来自较早的 i7-14700 平台，只作为 release window、调用形状和 state-shape
支持证据，不能与 i7-12700KF 上的绝对计时混合平均。

## 归档边界

早期 probe、候选复盘、冲突 revert、userfaultfd bridge 和被九点分解取代的中间消融
已经移出当前 evidence 入口。它们保留调查历史，但不再承担当前结论。

当前引用优先级为：

1. exact direct-parent/child A/B；
2. 九点机制分解；
3. matched fix/candidate validation；
4. `supporting/` 中的 release/state-shape 证据。
