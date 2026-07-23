# `cac1db8c3aad` folio lookup / batch discovery 分解计划

状态：已完成；九点 fresh-boot 矩阵及预注册分析全部通过。

## 已知边界

在同一台 i7-12700KF 裸机、同一个 64 MiB shared-dirty 4 KiB base-page workload
上，exact child 相对 direct parent 稳定慢约 `36.5%`。现有消融已经得到：

- 只消除通用 helper 的 out-of-line 调用没有恢复，排除了普通函数调用开销；
- 保留 folio lookup 和 batch discovery、仅给 `nr_ptes == 1` 恢复 parent-style
  commit/write/flush，可回收 `43.66%` 的原始缺口，仍比 parent 慢 `20.58%`；
- 较早的上界 probe 同时跳过 normal-path `vm_normal_folio()` 和随后的 trivial batch
  dispatch、但保留 generic commit，回收约 `38%`。该结果不能区分 lookup 与 batch。

本轮只分解最后两项：base-page normal path 中，剩余成本分别有多少来自
`mprotect_folio_pte_batch()` 的 batch discovery，以及更早的 `vm_normal_folio()`。

## 两个嵌套探针

两个探针都以 exact child `cac1db8c3aad97d6ffb56ced8868d6cbbbd2bfbe` 为基线，
并都保留上一轮已经验证的 parent-style 单 PTE commit/write/flush：

1. `folioonly`：仍执行 `vm_normal_folio()`，但在非 NUMA 路径固定 `nr_ptes=1`，不执行
   normal-path batch discovery；
2. `nofolio`：在同一路径同时跳过 `vm_normal_folio()` 与 batch discovery，再执行相同的
   单 PTE commit 路径。

因此：

- `fastpath -> folioonly` 只测去掉 normal-path batch discovery 的增量；
- `folioonly -> nofolio` 只测再去掉 folio lookup 的增量；
- `child -> nofolio` 测三个诊断改动合并后的总恢复；
- `nofolio -> parent` 是尚未解释的残余。

NUMA protection change 和大批次通用路径仍保留在源码中。这两个补丁只对固定的非 NUMA
base-page workload 做归因，会关闭对应路径的 batching，均不是候选修复。

## 机器码门禁

构建后必须验证：

- 两个 probe 都出现直接 `can_change_pte_writable()` 调用；
- `folioonly` 与 child 的 `change_pte_range()` 中 `vm_normal_folio()` 调用数相同；
- `nofolio` 恰好比 child 少一个 `vm_normal_folio()` 调用；
- 所有内核使用相同 canonical config、GCC/Kbuild 元数据、签名身份和等长 release string。

## 冻结 workload 与九点顺序

完全复用上一轮：64 MiB `MAP_SHARED | MAP_ANONYMOUS`，prefault 并写脏，4 KiB base
page，无 THP；每 process 1,000 次 read-only / restore / write-touch 循环，3 个外部
warm-up 加 15 个 measured，固定 P-core CPU 2、performance governor/EPP、Turbo 关闭、
`preempt=none`。返回值与页状态检查为硬 gate。

每点 fresh boot：

```text
parent A -> child A -> fastpath A -> folioonly A -> nofolio ->
folioonly B -> fastpath B -> child B -> parent B
```

嵌套双锚同时控制全程漂移和两个相邻消融的局部漂移。主指标仍为
`iteration_ns_per_page`，不根据中间结果改变 workload。

## 预注册判定

- 每点 CV 不超过 5%；
- parent、child、fastpath、folioonly 的 A/B 漂移绝对值均不超过 3%；
- exact child 回归至少 20%；
- 135 个 measured process 的语义、4 KiB 和 no-THP 检查全部通过；
- 全 15 轮与 drop-first 的各增量方向一致。

最终连续报告以下比例相对 exact parent-to-child 缺口的占比：generic commit 路径、batch
discovery、folio lookup、三项合并恢复和剩余残差。由于各项可能有代码布局与交互效应，
分量不强制精确相加，也不把诊断分支直接称为可提交修复。

## 结果

九个点的 `iteration_ns_per_page` 均值如下；数值越低越好：

| 点 | ns/page | CV |
| --- | ---: | ---: |
| parent A | 38.733 | 1.53% |
| child A | 52.933 | 1.12% |
| fastpath A | 46.800 | 0.88% |
| folioonly A | 46.933 | 0.55% |
| nofolio | 40.600 | 1.56% |
| folioonly B | 46.867 | 0.75% |
| fastpath B | 46.933 | 0.55% |
| child B | 53.000 | 0.71% |
| parent B | 38.867 | 1.33% |

双锚 midpoint 分析得到：

- exact child 相对 parent 回归 `+36.51%`；
- 恢复 parent-style 单 PTE commit/write/flush 后，相对 child 降低 `11.52%`，解释
  parent-to-child 原始绝对缺口的 `43.06%`；
- 在相同 commit fastpath 上跳过 normal-path batch discovery 没有改善：`folioonly`
  相对 `fastpath` 为 `+0.07%`，折算为原始缺口的 `-0.24%`，属于测量分辨率内；
- 再跳过 normal-path `vm_normal_folio()` 后，相对 `folioonly` 降低 `13.43%`，解释
  原始缺口的 `44.47%`；
- `nofolio` 相对 parent 只剩 `+4.64%`，三个诊断改动合计回收原始缺口的
  `87.29%`，未解释残差为 `12.71%`。

drop-first 分析方向和幅度一致：原始回归 `+36.24%`，commit path、batch discovery、
folio lookup 分别解释 `43.29%`、`-0.25%`、`44.81%`，合计回收 `87.85%`，相对
parent 剩余 `+4.40%`。

全部 135 个 measured process 都通过返回值、4 KiB base-page、no-THP 和语义检查；最大
CV 为 `1.56%`。parent、child、fastpath、folioonly 的 A/B 漂移分别为 `+0.34%`、
`+0.13%`、`+0.28%`、`-0.14%`。因此结果不是明显的时间漂移或单轮异常。

## 结论边界

对这个固定的 x86-64、shared-dirty、4 KiB base-page workload，`cac1db8c3aad` 的主要
回归成本可以分成两个近似同等大小的部分：通用 batch commit 路径约占 `43%`，普通路径
新增的 `vm_normal_folio()` lookup 约占 `44%`。`mprotect_folio_pte_batch()` 的 batch
discovery 本身在这个场景中没有可测成本。

这证明 `vm_normal_folio()` 是本场景的主要开销来源之一，但不证明该调用可以在通用内核
路径中直接删除。`nofolio` 会故意绕过 child 为 folio/batching 语义新增的工作，只是归因
探针，不是候选修复。剩余约 `13%` 的缺口也可能包含分支、循环或代码布局的交互效应；
在提出修复前仍需设计保留正确语义的 fastpath。

## 与源码热路径的对应

在这台 x86-64 机器的普通 4 KiB PTE 上，`vm_normal_folio()` 会进入
`vm_normal_page()`：从 PTE 提取 PFN，检查 special/zero/有效 PFN，把 PFN 转成
`struct page`，再转换成 folio。随后 `mprotect_folio_pte_batch()` 发现这是普通 order-0
folio，立即返回 `1`。

也就是说，这个 workload 每处理一个 4 KiB PTE 都支付一次 folio 查询，但没有得到批量
处理收益；protect 和 restore 两个阶段都会重复该路径。分项均值也与此吻合：parent 的
protect/restore 各约 `13 ns/page`，child 各约 `20 ns/page`，恢复 direct commit 后各约
`17 ns/page`，再跳过 normal-path folio lookup 后各约 `14 ns/page`。post-touch 一直约
`11--12 ns/page`，说明差值集中在两个 `mprotect()` 阶段，而不是后续写触碰。
