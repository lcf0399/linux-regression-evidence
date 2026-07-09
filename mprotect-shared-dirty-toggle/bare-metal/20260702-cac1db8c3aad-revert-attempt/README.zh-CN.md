# 2026-07-02 cac1db8c3aad revert 尝试

这个目录记录一次后续尝试：从之前的 attribution probe 往更接近 commit-level check 的方向推进。
目标提交是：

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

## 做了什么

我先把 `v6.16` 和 `v6.17` tag fetch 到本地 `linux-mm-unstable` git repo，然后从
真实 `v6.17` tag 新建临时 worktree。

直接执行：

```bash
git revert --no-commit cac1db8c3aad
```

结果仍然**不能 clean apply**，冲突位置在 `mm/mprotect.c`。

在 `cac1db8c3aad` 之后、`v6.17` 之前，仍有两个会触碰 `mm/mprotect.c` 的后续提交：

```text
cf1b80dc31a1 mm: pass page directly instead of using folio_page
8b2914162aa3 mm/mseal: small cleanups
```

所以之前反打失败不是单纯因为本地源码快照过时；真实 `v6.17` tag 上也已经有后续改动叠在
PTE batching change 上。

## mprotect-only minus-cac 候选 patch

因为 `cac1db8c3aad` 只修改 `mm/mprotect.c`，本轮合成了一个 `v6.17` mprotect-only
候选：保留 `v6.17` 全树，只把 `mm/mprotect.c` 改成 pre-`cac1db8c3aad` 的形状，
并叠上后续能兼容的 `mm/mprotect.c` cleanup context。

patch 文件：

```text
0001-v6.17-minus-cac1db8c3aad-mprotect-only-candidate.patch
```

这个 patch 会移除 `v6.17 change_pte_range()` present-PTE path 中由 batching 提交引入的
批量 commit/flush machinery。它比之前的 single-PTE probe 更接近 `v6.17 minus-cac`
候选，但它仍然是手工合成的 mprotect-only candidate，不是 clean exact `git revert`。

## 编译检查

这个候选 tree 通过了本地 object-level 编译检查，也通过了完整 `bzImage` build：

```bash
make -C linux-kernel-trees/sources/linux-v6.17-minus-cac-build olddefconfig
make -C linux-kernel-trees/sources/linux-v6.17-minus-cac-build -j$(nproc) mm/mprotect.o
make -C linux-kernel-trees/sources/linux-v6.17-minus-cac-build -j$(nproc) bzImage
```

结果：

```text
mm/mprotect.o build passed
arch/x86/boot/bzImage build passed
kernelrelease: 6.17.0-dirty
```

同一个候选随后也在真机上完成 build/install，安装后的 kernel release 是：

```text
6.17.0-bm-6.17-minus-cac1db8c3aad
```

安装后 `/boot` 仍低于本地设定的 90% 安全线。

## 真机 timing

我在真机上跑了同一个 shared-dirty full-toggle reproducer queue，把三个内核交错跑三轮：

```text
6.16 clean
6.17 clean
6.17 mprotect-only minus-cac candidate
```

主指标：`iteration_ns_per_page`，越低越好。

| Kernel | values | mean |
| --- | --- | ---: |
| `6.16.0-bm-6.16` | 25 25 25 | 25.000 |
| `6.17.0-bm-6.17` | 38 36 36 | 36.667 |
| `6.17.0-bm-6.17-minus-cac1db8c3aad` | 27 27 26 | 26.667 |

所有 step 都报告：

```text
expected_match_ratio=100
unexpected_results=0
smaps_*_kernel_page_kb=4
smaps_*_mmu_page_kb=4
smaps_*_anon_huge_kb=0
```

也就是说，这个合成的 mprotect-only minus-cac candidate 把 workload 从 `v6.17`
慢区间基本拉回到了接近 `v6.16` 的快区间。

这比之前 single-PTE probe 更强，但描述时仍然要谨慎：

- 直接 `git revert cac1db8c3aad` 在 `v6.17` 上冲突；
- 这次测试的是手工合成的 `mm/mprotect.c`-only minus-cac candidate；
- 因此它还不是 clean exact-revert A/B proof，不过 timing 已经很强地指向 batching
  change 是这个 workload 的相关机制。

## 文件

- `0001-v6.17-minus-cac1db8c3aad-mprotect-only-candidate.patch`：实际测试的
  mprotect-only candidate patch。
- `build-check.txt`：本地编译检查记录。
- `revert-attempt-summary.csv`：direct revert/build 状态总表。
- `step-summary.csv`：真机逐 step timing 结果。
- `aggregate-summary.csv`：真机聚合 timing 均值。
- `raw/mprotect_minus_cac_20260702/`：从真机队列复制回来的文本日志、summary 和运行环境。
