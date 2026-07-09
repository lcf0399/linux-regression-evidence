# 2026-07-06 folio-order state-shape 检查

这是 mprotect shared-dirty base-page workload 的归因 / state-shape 证据。
它不是新的 timing 结果，也不是独立的新 regression claim。

目的：回答一个可能的 caveat。`smaps` 显示 4 KiB/no THP 能证明 mapping 不是
PMD THP mapping，但单独看 `smaps` 不一定完全排除 “PTE-mapped large/compound
shmem folio”。这个检查对 workload 的每个页读取 `/proc/self/pagemap` 和
`/proc/kpageflags`。

## 方法

- workload 形态：64 MiB `MAP_SHARED | MAP_ANONYMOUS`，`MADV_NOHUGEPAGE`，
  检查前已经 write-touch。
- 内核：`6.16.0-bm-6.16`、`6.17.0-bm-6.17`、`7.1.0-bm-7.1`。
- 轮次：每个内核 3 轮，bare-metal interleaved queue。
- 检查项：
  - `smaps`：`KernelPageSize`、`MMUPageSize`、`AnonHugePages`、`THPeligible`
  - `pagemap`：present PFN
  - `kpageflags`：`KPF_COMPOUND_HEAD`、`KPF_COMPOUND_TAIL`、`KPF_HUGE`、`KPF_THP`

## 结果

| kernel | runs | present pages | compound_head sum | compound_tail sum | KPF_THP sum | KernelPageSize | MMUPageSize |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `6.16.0-bm-6.16` | 3 | 16384 each | 0 | 0 | 0 | 4 KiB | 4 KiB |
| `6.17.0-bm-6.17` | 3 | 16384 each | 0 | 0 | 0 | 4 KiB | 4 KiB |
| `7.1.0-bm-7.1` | 3 | 16384 each | 0 | 0 | 0 | 4 KiB | 4 KiB |

所有 9 轮均报告：

```text
present=16384
pfn_zero=0
compound_head=0
compound_tail=0
kpf_huge=0
kpf_thp=0
smaps_kernel_page_kb=4
smaps_mmu_page_kb=4
smaps_anon_huge_kb=0
smaps_thpeligible=0
```

因此，在这组内核和这个 workload 上，没有观察到 PTE-mapped compound/THP folio。
这支持把当前 workload 作为 order-0/base-page path 来做归因解释。

这不证明所有 shmem/THP policy 或所有机器都会有同样 folio 形态。

## 文件

- `summary.csv`：精简逐轮结果表。
- `mprotect_folio_order_probe.c`：standalone helper。
- `run_mprotect_folio_order_once.sh`：单内核运行脚本。
