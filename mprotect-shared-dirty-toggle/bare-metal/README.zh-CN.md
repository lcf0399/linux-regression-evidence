# mprotect shared-dirty toggle bare-metal 结果

更新时间：2026-07-21 UTC

这个目录保存 `mprotect()` standalone reproducer 的真机结果，包括 release-window
narrowing、源码归因 probe 和精确 direct-parent/child 实验。

## 2026-07-21 `cac1db8c3aad` 精确 A/B

结果目录：

```text
20260721-cac1db8c3aad-exact-ab/
```

在 i7-12700KF 真机上，三个 fresh boot 依次运行精确 direct parent、child、parent：

```text
45199f715b74 parent A -> cac1db8c3aad child -> 45199f715b74 parent B
```

| 点位 | n | `iteration_ns_per_page` 均值 | SD | CV |
| --- | ---: | ---: | ---: | ---: |
| parent A | 15 | 38.133 | 0.743 | 1.95% |
| child | 15 | 53.533 | 0.516 | 0.96% |
| parent B | 15 | 38.467 | 0.640 | 1.66% |

child 相对 parent 中点慢 `39.77%`，parent 漂移为 `0.87%`；drop-first-round
敏感性结果仍为 `+39.53%`。45 行测量全部通过语义和 4 KiB/no-THP 状态检查。
该提交只修改 `mm/mprotect.c`；两个内核使用相同归一化配置、工具链、Kbuild 元数据、
运行时 `preempt=none` 和 CPU profile。

因此，`cac1db8c3aad` 已被确认为这条限定 workload 中测得 slowdown 的来源，但这不把
结论扩大成 generic `mprotect()` 或真实应用整体回归。

## 早期 release-window 实验平台

```text
CPU: Intel Core i7-14700, 28 logical CPUs, 1 NUMA node
pinning: taskset -c 2
metric: iteration_ns_per_page, lower is better
scenario: shared_dirty_full_toggle_64m
mapping: 64 MiB shared dirty, 4 KiB pages, no THP
rounds: 9 external rounds
```

## bare-metal A/B

结果目录：

```text
20260622-20260623-ab/
```

| Kernel | Result dir | iteration_ns_per_page | protect | restore | post-touch | State |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `6.12.77-bm-6.12.77` | `20260622T170142Z_6.12.77-bm-6.12.77` | 27 | 9 | 8 | 9 | 4 KiB/no THP, semantic OK |
| `6.19.9-bm-6.19.9` | `20260622T165808Z_6.19.9-bm-6.19.9` | 37 | 14 | 14 | 8 | 4 KiB/no THP, semantic OK |
| `6.19.9-bm-6.19.9-pedro-v3` | `20260623T054659Z_6.19.9-bm-6.19.9-pedro-v3` | 39 | 15 | 15 | 8 | 4 KiB/no THP, semantic OK |
| `7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57` | `20260622T180559Z_7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57` | 39 | 15 | 15 | 8 | 4 KiB/no THP, semantic OK |

解读：

- 当前真机结果中，`6.19.9` 比 `6.12.77` 慢。
- `6.19.9 + Pedro v3 patch-only` 没有把该 standalone workload 拉回 `6.12.77` 水平，
  也没有比 `6.19.9` 原版改善。
- `mm-unstable-pedro-444fc9435e57` 不是严格 patch-only 对照；它只作为后续基线 context。

另有一次 `6.16` single-point smoke 只用于确认 standalone reproducer 和 state check 在
当前机器上能正常运行；该 smoke 不作为证据入口，raw 结果未保留。

## 2026-06-23 5-kernel queue context

结果目录：

```text
20260623-kernel-queue-5kernels-3rounds/
```

该队列用于早期真机 context：`6.12.77`、`6.19.9`、`6.19.9 + Pedro v3`、
`7.0.9` 和 `mm-unstable-pedro` 各跑 3 次。它说明 Pedro v3 patch-only 没有改善这条
standalone workload，但不负责 release-window narrowing。

## 2026-06-23 base-page attribution probe

结果目录：

```text
20260623-basepage-probe/
```

这不是 clean release kernel A/B，而是一个临时 probe patch：在 `6.19.9` 的
`change_pte_range()` 中为 resident base-page path 加一个 single-PTE fast path，用来
判断该 workload 的成本是否来自 base-page 路径经过 folio/batching helper。

三轮同 boot 结果：

```text
kernel                                n  iteration_mean  protect_mean  restore_mean  post_touch_mean
6.19.9-bm-6.19.9-basepage-probe       3          30.333        10.667        10.000            9.000
```

对照 `20260623-kernel-queue-5kernels-3rounds/` 中的 clean kernel：

```text
6.12.77-bm-6.12.77                    3          26.000         9.000         8.000            8.000
6.19.9-bm-6.19.9                      3          37.000        14.000        14.000            8.000
```

当前解释：probe 能追回一部分 `6.19.9` 原版成本，但没有完全回到 `6.12.77` 水平。
这支持继续把 root cause 聚焦在 base-page PTE loop 进入 folio/batching helper 的形状，
同时还需要 release-window 或更细 source attribution 找剩余差异。

## 2026-06-23 release-window narrowing

结果目录：

```text
20260623-narrow-6.16-6.19-3rounds/
```

这轮使用 5 个 clean kernel 交错运行 3 轮：

```text
6.16.0-bm-6.16
6.17.0-bm-6.17
6.18.0-bm-6.18
6.18.19-bm-6.18.19
6.19.9-bm-6.19.9
```

主指标仍是 `iteration_ns_per_page`，越小越好。汇总：

```text
kernel                 n  iteration_mean  iteration_cv_pct  values
6.16.0-bm-6.16        3          25.000             0.000  25 25 25
6.17.0-bm-6.17        3          37.000             0.000  37 37 37
6.18.0-bm-6.18        3          38.000             0.000  38 38 38
6.18.19-bm-6.18.19    3          38.000             0.000  38 38 38
6.19.9-bm-6.19.9      3          36.667             1.286  37 36 37
```

所有 step 都是 `expected_match_ratio=100`、`unexpected_results=0`。

这把 slowdown window 从早先的 `6.16..6.19.9` 缩小到 `v6.16 -> v6.17`
release window。它还不是 commit-level root cause；下一步应检查 `v6.16..v6.17`
中影响 `mprotect()` / PTE permission-change path 的提交。

外部 LKML 上已有独立讨论把类似 mprotect slowdown bisect 到
`cac1db8c3aad ("mm: optimize mprotect() by PTE batching")`；本目录的新结果和该方向一致，
但本目录自身仍只声明 release-window narrowing。

## 2026-06-24 6.17 single-PTE attribution probe

结果目录：

```text
20260624-6.17-singlepte-probe/
```

这不是 clean release kernel A/B，而是一个 attribution-only probe：在 `v6.17`
基础上把 `mm/mprotect.c::change_pte_range()` 的 present-PTE hot path 恢复成
single-PTE start/commit/flush 形状，用来检查 4 KiB shared-dirty base-page workload
的慢速是否主要来自 PTE batching 改写后的 hot-path 形状。

三次同 boot 复跑：

```text
kernel                              n  iteration_mean  values
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25
```

和 release-window narrowing 对齐看：

```text
6.16.0-bm-6.16                      3          25.000  25 25 25
6.17.0-bm-6.17                      3          37.000  37 37 37
6.17.0-bm-6.17-singlepte-probe      3          25.000  25 25 25
```

所有 probe run 语义检查通过，且 state-shape 仍是 4 KiB/no THP。

当前解释：这个 probe 把 `v6.17` 拉回 `v6.16` 快区间，因此强烈支持该 standalone
workload 的主要成本来自 `v6.17` PTE batching hot-path shape。不过它仍然是机制归因证据，
不是 upstream patch，也不是泛化的 `mprotect()` regression claim。

源码归因和 exact-revert caveat 见：

```text
20260624-6.17-singlepte-probe/source-attribution-note.zh-CN.md
```

## 2026-07-02 culprit-candidate 复盘

结果目录：

```text
20260702-culprit-candidate-review/
```

这个目录把当前源码层假设单独整理出来，方便维护者阅读。目前最强的候选提交是：

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

判断依据是：release-window narrowing 已经把 slowdown 缩到 `v6.16 -> v6.17`；
该提交修改 `change_pte_range()` hot path；而 v6.17 single-PTE attribution probe
能把该 workload 拉回 v6.16 timing 区间。

在当时，它还不是完整 `git bisect` 或 clean exact-revert A/B。上面的 2026-07-21
精确 direct-parent/child 实验现已补上这块归因缺口。

## 2026-07-02 cac1db8c3aad revert 尝试

结果目录：

```text
20260702-cac1db8c3aad-revert-attempt/
```

这次 follow-up 尝试从 attribution probe 往 exact commit-level check 推进。

在真实 `v6.17` tag 上直接执行 `git revert --no-commit cac1db8c3aad` 会在
`mm/mprotect.c` 冲突。主要原因看起来是同一个 release window 里后续还有
`mm/mprotect.c` 提交叠在 batching change 上：

```text
cf1b80dc31a1 mm: pass page directly instead of using folio_page
8b2914162aa3 mm/mseal: small cleanups
```

本轮合成了一个 `v6.17` mprotect-only minus-cac candidate patch，并确认它能通过
`mm/mprotect.o` object-level 编译检查和完整 `bzImage` build。随后把它安装到真机，
并跑了三内核交错队列：

```text
kernel                                      n  iteration_mean  values
6.16.0-bm-6.16                             3          25.000  25 25 25
6.17.0-bm-6.17                             3          36.667  38 36 36
6.17.0-bm-6.17-minus-cac1db8c3aad          3          26.667  27 27 26
```

所有 step 都是 `expected_match_ratio=100`、`unexpected_results=0`，并保持同一种
4 KiB/no THP state shape。

这个合成的 mprotect-only minus-cac candidate 当时把 workload 从 `v6.17` 慢区间基本
拉回 `v6.16` 快区间，仍可作为历史机制证据；但后续 direct-parent/child 结果现已成为
权威的 commit-level 归因。

## 2026-06-30 single-protect follow-up

结果目录：

```text
20260630-single-protect-followup/
```

这轮 follow-up 检查 slowdown 是否只出现在反复 protect/restore 循环中。每个 timed
iteration 重新创建 shared-dirty mapping，write-prefault 后只计时一次
`mprotect(PROT_READ)`。

主指标：`single_protect_ns_per_page`，越低越好。

```text
kernel                 n  single_protect_mean  values
6.16.0-bm-6.16        3                 8.000  8 8 8
6.17.0-bm-6.17        3                14.000  14 14 14
7.1.0-bm-7.1          3                17.000  18 15 18
```

所有 step 都报告 `expected_match_ratio=100`、`unexpected_results=0`。

这说明：在准备好的 shared-dirty range 上，单次 `mprotect(PROT_READ)` 也能看到
`v6.16 -> v6.17` slowdown。它是现有 mprotect 报告的补充证据，不是独立 claim。


## 2026-07-06 folio-order state-shape check

结果目录：

```text
20260706-folio-order-check/
```

这轮 follow-up 检查一个可能的 state-shape caveat：4 KiB smaps 输出可以排除 PMD THP
mapping，但单靠它不能完全排除 PTE-mapped compound folio。probe 对测试的 64 MiB
shared-dirty base-page 形态读取 smaps、pagemap 和 kpageflags。

在 `6.16.0-bm-6.16`、`6.17.0-bm-6.17`、`7.1.0-bm-7.1` 上各 3 轮
interleaved bare-metal 中，全部 9 轮均报告 16384 个 present pages、4 KiB
KernelPageSize/MMUPageSize，以及 `KPF_COMPOUND_HEAD`、`KPF_COMPOUND_TAIL`、
`KPF_HUGE`、`KPF_THP` 全为 0。

这支持把当前测试 workload 作为 order-0/base-page mprotect path 来做归因解释。

## 2026-07-09 userfaultfd bulk writeprotect bridge

结果目录：

```text
20260709-userfaultfd-bulk-wp-bridge/
```

这是一个相关入口检查，不是单独的 `mm/userfaultfd.c` regression claim。workload 对
1 GiB anonymous mapping 做 bulk `UFFDIO_WRITEPROTECT`，以进入同一
`change_protection()` / PTE write-protect 路径。

主指标：`protect_ns_avg / pages`，越低越好。

```text
kernel                                  mean ns/page
6.16.0-bm-6.16                          25.720
6.17.0-bm-6.17                          33.544
6.17.0-bm-6.17-minus-cac1db8c3aad       26.040
```

`minus-cac1db8c3aad` kernel 是 hand-adapted mprotect-only 机制候选，不是 clean
exact revert。但它能把 userfaultfd bulk-WP slowdown 拉回接近 `6.16` 区间，支持把
这个相关入口解释为继承同一 mprotect/PTE permission-change 机制信号。
