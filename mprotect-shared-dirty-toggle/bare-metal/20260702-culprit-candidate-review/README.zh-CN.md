# 2026-07-02 culprit 候选复盘

这个目录记录 mprotect shared-dirty base-page regression 的源码层缩窄结论。

它**不**声称已经完成完整 `git bisect` 或 exact revert。它的用途是把当前 culprit
假设单独拎出来，避免它埋在 attribution note 里，也避免和已经完成的真机 timing
证据混在一起。

## 当前状态

目前最强的候选提交是：

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

为什么它是当前候选：

- clean bare-metal release-window run 已经把 slowdown 缩到 `v6.16 -> v6.17`。
- 该提交位于这个 release window 内。
- 该提交修改 `mm/mprotect.c`，核心位置就是 `change_pte_range()` hot path。
- 它把 present-PTE permission update loop 从简单 single-PTE 形状改成
  batching / folio-helper 形状。
- 当前 workload 刻意限定为 4 KiB shared-dirty base pages、无 THP；也就是说，它会走
  base-page PTE 路径，但拿不到 large-folio batching amortization。
- v6.17 attribution-only single-PTE hot-path probe 能把 `iteration_ns_per_page`
  拉回 v6.16 快区间：

```text
6.16.0-bm-6.16                      25 25 25  mean=25.000
6.17.0-bm-6.17                      37 37 37  mean=37.000
6.17.0-bm-6.17-singlepte-probe      25 25 25  mean=25.000
```

single-protect follow-up 也说明 slowdown 不只是 repeated toggle loop 的 steady-state
副作用；单次 prepared-range `mprotect(PROT_READ)` 已经能复现：

```text
6.16.0-bm-6.16                       8  8  8  mean=8.000
6.17.0-bm-6.17                      14 14 14  mean=14.000
7.1.0-bm-7.1                        18 15 18  mean=17.000
```

## 仍然缺什么

这还不是 exact culprit proof：

- 没有对 `v6.16..v6.17` 做完整 `git bisect`；
- 没有测试 `cac1db8c3aad` 的 clean exact revert A/B；
- 没有声称该 series 里只有这一个 commit 贡献成本。

之前尝试把官方 patch 反打到本地 `linux-6.17` tree，没有干净应用：

```text
Hunk #3 FAILED at 177.
Hunk #4 FAILED at 302.
Hunk #5 FAILED at 318.
Hunk #6 FAILED at 350.
4 out of 6 hunks FAILED
```

所以对上游更准确的说法应该是：

```text
The slowdown appears aligned with the v6.17 PTE batching change, especially
cac1db8c3aad ("mm: optimize mprotect() by PTE batching").  I have not completed
an exact revert/bisect yet, but a v6.17 targeted single-PTE hot-path probe brings
this workload back to the v6.16 range.
```

也就是：已有很强的 candidate commit / series 方向，但还不能写成
“已经完整证明 culprit 就是该 commit”。

## 和已有证据的关系

- timing window：
  `../20260623-narrow-6.16-6.19-3rounds/`
- v6.17 attribution probe：
  `../20260624-6.17-singlepte-probe/`
- single-protect follow-up：
  `../20260630-single-protect-followup/`

这个目录是当前源码层 culprit hypothesis 的 maintainer-facing 摘要，不是新的 timing 实验。
