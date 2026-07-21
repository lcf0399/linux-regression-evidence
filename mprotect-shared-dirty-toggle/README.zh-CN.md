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

当前最强证据是在 i7-12700KF 真机上围绕
`cac1db8c3aad ("mm: optimize mprotect() by PTE batching")` 完成的精确
direct-parent/child 夹心：

| 点位 | 提交 | n | `iteration_ns_per_page` 均值 |
| --- | --- | ---: | ---: |
| parent A | `45199f715b74` | 15 | 38.133 |
| child | `cac1db8c3aad` | 15 | 53.533 |
| parent B | `45199f715b74` | 15 | 38.467 |

child 相对两个 parent 的均值中点慢 `39.77%`，而 parent 漂移只有 `0.87%`；每点
去掉第一轮后仍慢 `39.53%`。45 个 measured process 的语义与 4 KiB/no-THP 状态
检查全部通过。

因此，这已经是该限定 workload 的 exact commit-level culprit evidence；它仍不代表
generic `mprotect()` 或真实应用整体回归。完整源码/构建身份和原始测量在：

```text
bare-metal/20260721-cac1db8c3aad-exact-ab/
```

更早的 i7-14700 standalone rerun 把 slowdown 缩小到 `v6.16 -> v6.17`
release window，并提供下面的版本背景。

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

这份复盘当时把 `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`
列为最强候选。上面的后续精确 direct-parent/child 实验现已确认：对这条 workload，
测得的 slowdown 就由该提交引入；commit 归因不再需要完整 bisect，也不再依赖有冲突的
v6.17 revert。

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

## 早期非真机背景

早期 QEMU/lab 结果只用于筛选这个 workload，并指导后续真机实验。其详细结果、profile、
state audit 和实验框架文件继续保留在本地，但不属于当前公开证据包。上面的公开结论只依赖
`bare-metal/` 中的真机证据。

## 目录

- `bare-metal/`：真机结果和源码归因证据。
- `reproducer/`：给维护者快速检查用的 standalone C reproducer 和辅助脚本，不依赖
  experiment framework。
- `workload/`：为语义审计保留的用户态 workload source。
