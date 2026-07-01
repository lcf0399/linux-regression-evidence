# 6.17 single-PTE probe source attribution note

更新时间：2026-06-24 UTC

这份 note 解释 `6.17.0-bm-6.17-singlepte-probe` 和疑似引入提交之间的关系。
它的用途是避免把当前实验误写成“已经做了 exact revert”，同时保留足够强的源码归因证据。

## 对齐的上游提交

外部 LKML 讨论中提到的候选提交是：

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

该提交只改 `mm/mprotect.c`，核心变化是把 `change_pte_range()` 从单 PTE 修改路径改成
PTE batching 形状：

- `mprotect_folio_pte_batch()` 从普通 `folio_pte_batch()` 改成带 flags 的
  `folio_pte_batch_flags()`。
- 新增 `prot_commit_flush_ptes()`、`page_anon_exclusive_sub_batch()`、
  `commit_anon_folio_batch()`、`set_write_prot_commit_flush_ptes()` 等 helper。
- present-PTE path 里新增 `nr_ptes`、folio/page 查询、batch start/commit/flush。
- loop 从固定 `pte++` / `addr += PAGE_SIZE` 变成
  `pte += nr_ptes` / `addr += nr_ptes * PAGE_SIZE`。

对我们的 workload 来说，映射是 4 KiB shared-dirty base-page，不是大 folio。也就是说，
有效 batch 在当前场景里不会带来“大批量摊销”的收益；如果新 generic batching shape
让单页 hot path 多走 helper/branch，就可能表现为每页成本升高。

## 为什么不是 exact revert

我试过在 `linux-6.17` tree 上直接反打官方 patch：

```text
patch --dry-run -R -p1 < /tmp/cac1db8c3aad.patch
```

结果：

```text
checking file mm/mprotect.c
Hunk #3 FAILED at 177.
Hunk #4 FAILED at 302.
Hunk #5 FAILED at 318.
Hunk #6 FAILED at 350.
4 out of 6 hunks FAILED
```

所以当前没有声称“完整 exact revert 已经应用并测试”。更准确的说法是：

```text
commit-aligned source probe / attribution-only probe
```

也就是：根据该提交在 `change_pte_range()` 中引入的 hot-path shape，手工恢复当前
workload 会命中的 present-PTE 单页路径，然后看性能是否回到旧版本区间。

## probe 做了什么

`0001-mm-mprotect-probe-6.17-single-pte-hotpath.patch` 在 `v6.17` 基础上只改
`mm/mprotect.c::change_pte_range()` 的 present-PTE path：

- 去掉当前路径里的 `nr_ptes` batching 推进。
- 不再调用 `mprotect_folio_pte_batch()` 来决定本轮 PTE 数。
- 当前 shared-dirty base-page path 回到单页 `ptep_modify_prot_start()` /
  `ptep_modify_prot_commit()` / `tlb_flush_pte_range(..., PAGE_SIZE)` 形状。
- loop 回到 `pte++` / `addr += PAGE_SIZE`。

这不是准备发给上游的修复补丁。它是为了回答一个归因问题：

```text
如果只把当前 workload 命中的 present-PTE hot path 恢复成单页形状，
v6.17 的慢速是否会消失？
```

结果是会消失：

```text
6.16.0-bm-6.16                      25 25 25  mean=25.000
6.17.0-bm-6.17                      37 37 37  mean=37.000
6.17.0-bm-6.17-singlepte-probe      25 25 25  mean=25.000
```

因此当前证据强度可以表述为：

```text
release-window narrowing + source-diff alignment + targeted hot-path probe
```

它支持“`v6.17` PTE batching hot-path shape 是该 standalone workload 的主要成本来源”。
但在没有 exact revert 或 git bisect 的情况下，不应写成“本地已经完整证明
`cac1db8c3aad` 是唯一 culprit commit”。
