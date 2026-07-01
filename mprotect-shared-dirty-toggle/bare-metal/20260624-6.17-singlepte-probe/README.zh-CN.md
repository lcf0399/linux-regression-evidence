# 2026-06-24 mprotect 6.17 single-PTE probe

这个目录记录一个 attribution-only probe，不是 upstream patch，也不是 clean release
kernel A/B。它用于检查 `v6.17` 后 `mprotect()` shared-dirty 4 KiB PTE workload
变慢，是否主要来自 `change_pte_range()` 中新增的 PTE batching hot-path 形状。

场景：

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`
- `PIN_CPU=2`
- metric: `iteration_ns_per_page`，越小越好

probe kernel：

```text
6.17.0-bm-6.17-singlepte-probe
```

这个 probe 在 `v6.17` 基础上只改 `mm/mprotect.c::change_pte_range()` 的
present-PTE path，把当前 shared-dirty base-page 场景会走到的部分恢复成单 PTE
start/commit/flush 形状。补丁保存在：

```text
0001-mm-mprotect-probe-6.17-single-pte-hotpath.patch
```

源码归因说明见：

```text
source-attribution-note.zh-CN.md
```

## 结果

三次同 boot 复跑：

```text
kernel                              n  iteration_mean  values  semantic
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25  OK
```

对照前一轮 release-window narrowing：

```text
kernel                              n  iteration_mean  values
6.16.0-bm-6.16                      3          25.000  25 25 25
6.17.0-bm-6.17                      3          37.000  37 37 37
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25
```

所有 probe run 都是：

```text
expected_match_ratio=100
unexpected_results=0
smaps_before_kernel_page_kb=4
smaps_before_mmu_page_kb=4
smaps_before_anon_huge_kb=0
smaps_after_kernel_page_kb=4
smaps_after_mmu_page_kb=4
smaps_after_anon_huge_kb=0
```

## 解读

这个结果强烈支持当前工作假设：该 standalone workload 的 `v6.16 -> v6.17`
slowdown 主要来自 `change_pte_range()` 中 PTE batching 改写后，4 KiB
shared-dirty base-page hot path 多走了 batching/folio helper 形状；在这个场景里
有效 batch 仍然是单页，所以额外路径成本没有被 batch amortization 抵消。

需要注意：这不是 exact revert。官方 `cac1db8c3aad ("mm: optimize mprotect() by
PTE batching")` patch 反打到当前 `linux-6.17` tree 时有 4 个 hunk 不匹配。因此这里的
结论应写成 commit-aligned source probe / attribution-only probe，而不是完整
commit revert。

这仍然不是泛化的 `mprotect()` regression claim。当前 claim 只限定在：

```text
mm/mprotect.c::change_pte_range()
shared_dirty_full_toggle_64m
4 KiB PTE mapping
full-range repeated protection toggle
bare-metal i7-14700 node
```

如果后续要准备上游邮件，应该把这个目录表述为机制归因证据，而不是直接当作 proposed
fix 或泛化性能结论。
