# 2026-07-06 Folio-order State-shape Check

This is attribution/state-shape evidence for the mprotect shared-dirty
base-page workload.  It is not a new timing result and not a separate
regression claim.

The purpose is to answer one possible caveat: `smaps` showing 4 KiB/no THP
proves that the mapping is not a PMD THP mapping, but by itself it does not
fully rule out a PTE-mapped large/compound shmem folio.  This check reads
`/proc/self/pagemap` and `/proc/kpageflags` for the workload pages.

## Method

- Workload shape: 64 MiB `MAP_SHARED | MAP_ANONYMOUS`, `MADV_NOHUGEPAGE`,
  write-touched before inspection.
- Kernels: `6.16.0-bm-6.16`, `6.17.0-bm-6.17`, `7.1.0-bm-7.1`.
- Runs: 3 interleaved bare-metal rounds per kernel.
- Checks:
  - `smaps`: `KernelPageSize`, `MMUPageSize`, `AnonHugePages`, `THPeligible`
  - `pagemap`: present PFNs
  - `kpageflags`: `KPF_COMPOUND_HEAD`, `KPF_COMPOUND_TAIL`, `KPF_HUGE`, `KPF_THP`

## Result

| kernel | runs | present pages | compound_head sum | compound_tail sum | KPF_THP sum | KernelPageSize | MMUPageSize |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `6.16.0-bm-6.16` | 3 | 16384 each | 0 | 0 | 0 | 4 KiB | 4 KiB |
| `6.17.0-bm-6.17` | 3 | 16384 each | 0 | 0 | 0 | 4 KiB | 4 KiB |
| `7.1.0-bm-7.1` | 3 | 16384 each | 0 | 0 | 0 | 4 KiB | 4 KiB |

All nine runs reported:

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

So, for this tested workload on these kernels, I did not observe a PTE-mapped
compound/THP folio.  This supports treating the workload as an order-0/base-page
path for attribution purposes.

This does not prove that every shmem/THP policy or every system would have the
same folio shape.

## Files

- `summary.csv`: compact per-run table.
- `mprotect_folio_order_probe.c`: standalone helper.
- `run_mprotect_folio_order_once.sh`: one-kernel runner used by the queue.
