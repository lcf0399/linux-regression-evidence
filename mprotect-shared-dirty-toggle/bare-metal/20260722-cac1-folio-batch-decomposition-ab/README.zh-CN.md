# `cac1db8c3aad` folio lookup / commit-path 九点分解

本实验在精确 child `cac1db8c3aad` 上用三个诊断 probe 分离三项成本：

1. generic single-PTE commit/write/flush 路径；
2. normal-path batch discovery；
3. normal-path `vm_normal_folio()` lookup。

九个 fresh-boot 点为：

```text
parent A -> child A -> fastpath A -> folioonly A -> nofolio ->
folioonly B -> fastpath B -> child B -> parent B
```

| 角色 | midpoint `ns/page` |
| --- | ---: |
| parent | 38.800 |
| child | 52.967 |
| fastpath | 46.867 |
| folioonly | 46.900 |
| nofolio | 40.600 |

相对 parent-to-child 原始绝对缺口，single-PTE commit path 解释 `43.06%`，
batch discovery 为 `-0.24%`，folio lookup 解释 `44.47%`；合计解释 `87.29%`。
drop-first 结果方向和幅度一致。135 个 measured process 全部通过返回值、4 KiB
page-state 和 no-THP gate。

这些 probe 都绕过部分通用语义，只用于归因，不是拟议修复。完整运行顺序和预注册
判定见 `experiment-plan.zh-CN.md`；聚合数字见 `summary.tsv`、
`component-summary.tsv`、`sensitivity.tsv` 和 `decision.tsv`。

本目录包含 `fastpath`、`folioonly` 和 `nofolio` 使用的三个诊断 patch；
`prepare_build_install_folio_batch_probes.sh` 可从同一个精确 child 源码重新构建
三者。
