# 2026-07-09 userfaultfd bulk writeprotect bridge

这是 mprotect shared-dirty regression 线程的相关机制证据，不是单独的
`mm/userfaultfd.c` regression claim。

问题是：另一个进入同一 PTE permission-change 机制的入口，是否也能看到同样的
`v6.16 -> v6.17` 成本增长。

## workload

- 场景：`bulk_writeprotect_ioctl_1024m`
- 映射：1 GiB anonymous mapping
- 操作：注册 userfaultfd write-protect range，然后对整段做 bulk
  `UFFDIO_WRITEPROTECT` set/clear。
- pin：CPU 2
- 轮数：每个 kernel 5 个 interleaved bare-metal batch

## kernels

- `6.16.0-bm-6.16`
- `6.17.0-bm-6.17`
- `6.17.0-bm-6.17-minus-cac1db8c3aad`

其中 `minus-cac1db8c3aad` 是 hand-adapted mprotect-only 机制候选，用来检查
PTE permission-change 路径。它不是 clean exact `git revert`，也不是修复 patch。

## 结果

主指标：`protect_ns_avg / pages`，单位 ns/page。

| kernel | precise ns/page samples | mean | min | max | integer samples |
| --- | ---: | ---: | ---: | ---: | --- |
| `6.16.0-bm-6.16` | 26.361, 23.196, 26.622, 26.142, 26.277 | 25.720 | 23.196 | 26.622 | 26, 23, 26, 26, 26 |
| `6.17.0-bm-6.17` | 32.688, 32.703, 34.102, 34.139, 34.087 | 33.544 | 32.688 | 34.139 | 32, 32, 34, 34, 34 |
| `6.17.0-bm-6.17-minus-cac1db8c3aad` | 27.410, 27.368, 24.161, 27.265, 23.996 | 26.040 | 23.996 | 27.410 | 27, 27, 24, 27, 23 |

相对 `6.16` mean：

- `6.17`：约 `+30.4%`
- `6.17-minus-cac1db8c3aad`：约 `+1.2%`

所有 15 个 run 均报告：

```text
expected_match_ratio=100
unexpected_results=0
errno_eperm=0
errno_einval=0
errno_enoent=0
errno_enomem=0
errno_eexist=0
errno_other=0
```

## 解释

这个 userfaultfd bulk write-protect workload 里的 `6.17` slowdown，基本也能被同一个
mprotect/PTE permission-change 机制候选拉回 v6.16 区间。

因此它更适合作为同一 `change_protection()` / PTE update 机制的另一个入口检查，而不是
独立的 `mm/userfaultfd.c` regression。

由于 `minus-cac1db8c3aad` 不是 clean exact revert，这仍不能证明单个 culprit commit。
