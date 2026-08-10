# 裸机结果

## Release screen

测试顺序为 fresh-boot
`v6.12.95 A -> v7.1.3 -> v6.12.95 B`。只有
`L0_STANDARD_LIFECYCLE` 同时通过 old-faster、drop-first、控制漂移和 CV 门：

| old A | v7.1.3 | old B | new vs midpoint | drop first | old drift | max CV |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8299.366 | 9121.969 | 8161.576 | `+10.832%` | `+10.761%` | `-1.660%` | `2.171%` |

完整四 profile 摘要保存在 [`release-summary.tsv`](release-summary.tsv)，未晋级的
R0、N0 和 A0 仍保留为负结果或环境控制，不纳入本回归 claim。

## 精确提交对照

两组均采用 fresh-boot `parent A -> child -> parent B`：

| pair | parent -> child | parent A | child | parent B | delta | drop first | drift | max CV |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SQ | `02255d55260a` -> `8078486e1d53` | 8447.193 | 8874.057 | 8526.431 | `+4.563%` | `+4.627%` | `+0.938%` | `2.061%` |
| CQ | `8078486e1d53` -> `81a4058e0cd0` | 8851.177 | 9155.872 | 8924.005 | `+3.019%` | `+2.909%` | `+0.823%` | `2.362%` |

`8078486e1d53` 在两组独立启动序列中分别作为 SQ child 和 CQ parent，测得均值只差
`0.153%`，为两组对照的内部一致性检查。90 个精确提交 measured row 和 45 个 release
L0 measured row 全部通过 setup、mmap、NOP、munmap、close、ring health、CPU 和语义检查。

## 环境与身份

- Intel Core i7-12700KF，32 GiB RAM；固定 P-core logical CPU 2；
- governor 与 EPP 均为 `performance`，Turbo 关闭，正式计时 trace-off；
- 每点 3 个 warm-up 和 15 个 measured round，每轮 512 个 lifecycle；
- 所有比较点的 requested/build/actual preempt 均为 `none/dynamic/full`，实际运行模式
  在全部点匹配为 `full`；
- 精确 pair 使用相同 canonical config、GCC 15.2.0、Kbuild metadata、等长 release
  string 和相同 workload binary；
- 六个精确 pair boot ID 与运行时 Build ID 均独立并匹配预期。

非计时 CQ-child trace 直接记录 `io_create_region()` 68 次、`io_free_region()` 102 次和
`io_uring_mmap()` 64 次，见 [`direct-hit.tsv`](direct-hit.tsv)。它只证明 workload 命中
目标路径，不参与计时。

## 文件

- [`release-measured-rounds.tsv`](release-measured-rounds.tsv)：release L0 的 45 个入选
  measured row；
- [`exact-measured-rounds.tsv`](exact-measured-rounds.tsv)：两个 exact pair 的 90 个入选
  measured row；
- [`release-run-identity.tsv`](release-run-identity.tsv) 与
  [`exact-run-identity.tsv`](exact-run-identity.tsv)：启动、构建、preempt 和 workload 身份；
- [`source-identity.tsv`](source-identity.tsv)：三个相邻源码点；
- [`workload-identity.tsv`](workload-identity.tsv)：冻结输入哈希。
