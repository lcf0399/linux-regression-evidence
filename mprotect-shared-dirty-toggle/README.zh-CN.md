# mprotect shared-dirty toggle 证据

这个目录当前支撑面向上游的报告：

```text
[REGRESSION] mm/mprotect: shared-dirty base-page toggle slower since v6.17
```

## 结论范围

这是一个刻意收窄的用户态可见 `mprotect()` workload：

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping
- 已经 prefault 并写脏的 4 KiB base pages，无 THP
- 反复对整段 mapping 做 `mprotect(PROT_READ)`
- 再用 `mprotect(PROT_READ | PROT_WRITE)` 恢复写权限
- 每轮 protect/restore 后再 write-touch

它不声称存在泛化的 `mprotect()` regression，也不声称 `anon_full_toggle` 或 THP mprotect regression。

## 当前真机结论

当前最适合给维护者看的证据，是 i7-14700 真机上的 standalone rerun。它把 slowdown
缩小到 `v6.16 -> v6.17` release window。

主指标：`iteration_ns_per_page`，越低越好。

| Kernel | values | mean |
| --- | --- | ---: |
| `v6.16` | 25 25 25 | 25.000 |
| `v6.17` | 37 37 37 | 37.000 |
| `v6.18` | 38 38 38 | 38.000 |
| `v6.18.19` | 38 38 38 | 38.000 |
| `v6.19.9` | 37 36 37 | 36.667 |

所有 run 都报告 `expected_match_ratio=100`、`unexpected_results=0`。

一个 attribution-only 的 v6.17 single-PTE probe 能把 standalone 结果拉回 v6.16
快区间：

| Kernel | values | mean |
| --- | --- | ---: |
| `v6.16` | 25 25 25 | 25.000 |
| `v6.17` | 37 37 37 | 37.000 |
| `v6.17 single-PTE probe` | 25 25 25 | 25.000 |

这个 probe 不是 exact commit revert，也不是要提交给上游的 patch。它只是机制归因证据，
指向该 workload 在 `mm/mprotect.c::change_pte_range()` 中的 v6.17 PTE-batching
hot-path shape。

新增的 culprit-candidate review 单独记录当前源码层假设：

```text
bare-metal/20260702-culprit-candidate-review/
```

目前最强的候选是 `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`。
这是一条很强的 candidate commit / series 方向，但还不是 exact culprit proof：目前
还没有完成完整 `git bisect`，也没有做 clean exact-revert A/B。

后续 revert 尝试记录在：

```text
bare-metal/20260702-cac1db8c3aad-revert-attempt/
```

在真实 `v6.17` tag 上直接执行 `git revert --no-commit cac1db8c3aad` 会因为后续
`mm/mprotect.c` 改动叠在上面而发生冲突。合成的 `v6.17` mprotect-only minus-cac
candidate 已经通过 `mm/mprotect.o` 编译检查和完整 `bzImage` build，并完成真机
interleaved timing：

| Kernel | iteration_ns_per_page values | mean |
| --- | ---: | ---: |
| `6.16.0-bm-6.16` | 25 25 25 | 25.000 |
| `6.17.0-bm-6.17` | 38 36 36 | 36.667 |
| `6.17.0-bm-6.17-minus-cac1db8c3aad` | 27 27 26 | 26.667 |

这个结果把 `v6.17` 慢区间基本拉回 `v6.16` 快区间，是目前最强的
`cac1db8c3aad` 机制归因证据。不过它仍然不是 clean exact-revert A/B：测试内核是
手工合成的 `mm/mprotect.c`-only minus-cac candidate。

`v6.19.9 + Pedro v3 patch-only` 和后续 mm-unstable/Pedro follow-up 都没有改善这条
standalone bare-metal 结果。

## 2026-06-30 single-protect 补充

为了检查信号是否只来自反复 `RW -> R -> RW` toggle，我补了一个更窄的 follow-up：
每个 timed iteration 重新准备一个 shared-dirty mapping，只计时一次
`mprotect(PROT_READ)`。

结果目录：

```text
bare-metal/20260630-single-protect-followup/
```

主指标：`single_protect_ns_per_page`，越低越好。

| Kernel | values | mean | vs v6.16 |
| --- | --- | ---: | ---: |
| `v6.16` | 8 8 8 | 8.000 | baseline |
| `v6.17` | 14 14 14 | 14.000 | +75.0% |
| `v7.1` | 18 15 18 | 17.000 | +112.5% |

所有 run 都报告 `expected_match_ratio=100`、`unexpected_results=0`。

这说明：在准备好的 shared-dirty range 上，单次 `mprotect(PROT_READ)` 本身已经复现
`v6.16 -> v6.17` slowdown。它是同一条 `mprotect()` PTE update path 的补充证据，
不是独立的新 regression claim。

后续 folio-order state-shape 检查记录在：

```text
bare-metal/20260706-folio-order-check/
```

它对同一个 64 MiB shared-dirty base-page workload 读取 pagemap/kpageflags。
在 `6.16.0-bm-6.16`、`6.17.0-bm-6.17`、`7.1.0-bm-7.1` 上共 9 轮均显示：
16384 个 present pages，`KernelPageSize` / `MMUPageSize` 都是 4 KiB，并且
`KPF_COMPOUND_HEAD`、`KPF_COMPOUND_TAIL`、`KPF_THP` 都为 0。这是 state-shape
归因证据，说明当前测试的 workload 不是 PTE-mapped compound/THP folio 场景。

相关的 userfaultfd bulk write-protect bridge 记录在：

```text
bare-metal/20260709-userfaultfd-bulk-wp-bridge/
```

这不是单独的 `mm/userfaultfd.c` regression claim，而是检查另一个进入同一 PTE
permission-change 机制的入口。`bulk_writeprotect_ioctl_1024m` 的 5 个 interleaved
bare-metal batch 结果为 `6.16=25.720`、`6.17=33.544`、
`6.17-minus-cac1db8c3aad=26.040 ns/page`。`minus-cac` kernel 是 hand-adapted
mprotect-only 机制候选，不是 clean exact revert，但它也能把 userfaultfd bulk-WP
结果拉回接近 `6.16` 的区间。

另外，探索性的 `mmap_lock` 和 `mmu_notifier` 路线也观察到 timing signal，但
split/no-KVM/KVM attribution 显示主差异仍回到 `mprotect()` permission-change /
restore 路径。它们只保留为 supporting attribution，不作为独立上游 claim。

## 早期 Lab/QEMU 背景

早期 formal lab timing 显示 `v6.19.9` 慢于 `v6.12.77`。这部分现在保留为历史候选证据
和背景，不再作为当前面向上游的主结论。

`cycle_ns_per_page`：

| CPU | v6.12.77 | v6.19.9 | delta | reliability |
| --- | ---: | ---: | ---: | --- |
| 1 | 346.8 | 578.1 | -40.0% | clean reliable |
| 2 | 394.7 | 641.7 | -38.5% | robust-only |
| 4 | 381.1 | 624.8 | -39.0% | partial same direction |

单独做过的 release-level sanity check 显示 `v6.18.19` 已经进入慢区间，但这些 raw run
没有放入当前精简公开证据包。

## 早期 mm-unstable Lab Follow-up

David Hildenbrand 指向了 Pedro Falcato 最近的 small-folio mprotect optimization。
针对 `akpm/mm mm-unstable 444fc9435e57` 的 lab sanity matrix 显示，这个 workload
里出现了部分缓解，但没有回到 `v6.12.77` 的 timing：

这一节是早期 QEMU/lab follow-up context，不应和上面的后续真机 standalone 结果混在一起。

| CPU | v6.12.77 | v6.19.9 | mm-unstable | mm-unstable vs v6.19 | gap closed |
|---:|---:|---:|---:|---:|---:|
| 1 | 336.1 | 532.0 | 497.0 | 6.6% faster | 17.9% |
| 2 | 369.2 | 581.9 | 503.3 | 13.5% faster | 36.9% |
| 4 | 355.7 | 587.2 | 524.2 | 10.7% faster | 27.2% |
| 8 | 369.7 | 583.6 | 534.2 | 8.5% faster | 23.1% |
| 16 | 374.8 | 607.1 | 547.8 | 9.8% faster | 25.5% |

该 sanity matrix 中 16 CPU 行有一次 `v6.12.77` QEMU failure，因此它只是辅助趋势证据。

单独的 state-shape audit 检查了这个 mprotect 对比是否存在类似 `MADV_PAGEOUT` 的 caveat，
即不同内核是否在 materially different page/VMA state 上运行。state audit 发现成功的
`v6.12.77`、`v6.19.9` 和 `mm-unstable` run 都使用同一种 4 KiB shared-dirty PTE
mapping 形态：protect 前后都是 16384 个 present pages、无 THP backing、最终一个 VMA、
没有 semantic mismatch。这支持把剩余 timing gap 视为 same-state implementation-path
cost，而不是 workload-state mismatch comparison。

## 目录

- `workload/`：框架使用的 generated workload source。
- `reproducer/`：给维护者快速检查用的 standalone C reproducer 和辅助脚本，不依赖
  experiment framework。
- `reproducer-validation/`：standalone reproducer 的 lab 验证总结。
- `experiments/`：formal experiment profile。
- `formal-lab-summary/`：最早 formal lab timing 和 direct-hit coverage 证据的公开
  精简摘要。原始 runner output 已移到本地忽略的 `local-archive/`。
- `mm-unstable-followup-summary/`：small-folio optimization 讨论使用的轻量 follow-up
  summary。原始 lab/local raw 已移到本地忽略的 `local-archive/`。
- `state-audit-summary/`：支持 same-state comparison assumption 的 lab
  state-shape audit 公开精简摘要。原始 lab output 已移到本地忽略的
  `local-archive/`。
- `bare-metal/`：新 i7-14700 节点上的真机复跑结果。当前 standalone A/B 中，
  `6.19.9` 相对 `6.12.77` 仍较慢；`6.19.9 + Pedro v3 patch-only` 未改善该
  standalone 结果。后续的 base-page attribution probe 能追回一部分 `6.19.9`
  原版成本，但没有完全回到 `6.12.77` 水平。后续 release-window narrowing 显示
  slowdown 出现在 `v6.16 -> v6.17` 窗口；`v6.17` single-PTE attribution probe
  能把该 standalone metric 拉回 `v6.16` 快区间，支持把主要成本聚焦到
  `mm/mprotect.c::change_pte_range()` 的 v6.17 PTE-batching hot-path shape。
  该 probe 不是 exact commit revert；细节见
  `bare-metal/20260624-6.17-singlepte-probe/source-attribution-note.zh-CN.md`。
  `bare-metal/20260702-culprit-candidate-review/` 单独记录当前最强候选
  `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`，并明确保留
  no-full-bisect / no-exact-revert caveat。
  `bare-metal/20260702-cac1db8c3aad-revert-attempt/` 记录第一次后续 exact-revert
  尝试：直接 git revert 在 `v6.17` 上冲突；合成的 mprotect-only minus-cac candidate
  能 build 出 `bzImage`，并在真机 interleaved timing 中把 `v6.17` 慢区间基本拉回
  `v6.16` 快区间。该结果是强机制归因证据，但仍不是 clean exact-revert A/B。
  后续 `bare-metal/20260630-single-protect-followup/` 说明单次 protect 操作本身也能
  复现同一个 release-window slowdown。`bare-metal/20260706-folio-order-check/` 记录
  no-compound/no-THP state-shape check；`bare-metal/20260709-userfaultfd-bulk-wp-bridge/`
  记录相关 userfaultfd bulk-WP cross-entry check，并显示同一个 mprotect-only
  minus-cac 机制候选也能把该入口拉回旧版本区间。

formal lab 和 follow-up matrix 的 public bundle 只保留精简指标汇总。完整 runner
目录、raw CSV/JSON、pipeline metadata 和冗长 launch logs 保留在本地
`local-archive/`，默认不上传，除非后续 debug 需要。
