# single-protect 真机补充结果

这是 `mprotect-shared-dirty-toggle` 上游报告的补充证据。它检查 slowdown 是否只出现在
反复 `RW -> R -> RW` toggle 循环中，还是只做一次 `mprotect(PROT_READ)` 就已经变慢。

它不是独立的新 regression claim，而是同一条 `mm/mprotect.c` 权限修改路径的 supporting
evidence。

## workload

每个 timed iteration 做：

1. `mmap(MAP_SHARED | MAP_ANONYMOUS, PROT_READ | PROT_WRITE)`；
2. `MADV_NOHUGEPAGE`；
3. 对 64 MiB range 全部 write-prefault；
4. 用 `/proc/self/smaps` 检查 state-shape；
5. 只计时一次 `mprotect(PROT_READ)`；
6. `munmap()`。

主指标是 `single_protect_ns_per_page`，越低越好。`setup` 和 `total` 只作辅助检查。

## 运行参数

```text
CPU: Intel Core i7-14700, 28 logical CPUs, 1 NUMA node
pinning: taskset -c 2
mapping: 64 MiB shared dirty, 4 KiB pages, no THP
iterations: 每个 external round 200 个 timed iterations
warmup: 5 iterations
external rounds: 每个 boot/run step 5 轮
queue: v6.16 -> v6.17 -> v7.1，重复 3 次
```

## 结果

汇总：

| Kernel | n | `single_protect_ns_per_page` values | mean | vs v6.16 | state |
| --- | ---: | --- | ---: | ---: | --- |
| `v6.16` | 3 | 8 8 8 | 8.000 | baseline | 4 KiB/no THP, semantic OK |
| `v6.17` | 3 | 14 14 14 | 14.000 | +75.0% | 4 KiB/no THP, semantic OK |
| `v7.1` | 3 | 18 15 18 | 17.000 | +112.5% | 4 KiB/no THP, semantic OK |

所有 step 都报告 `expected_match_ratio=100`、`unexpected_results=0`。

解读：

- 单次 `mprotect(PROT_READ)` 本身已经出现 `v6.16 -> v6.17` slowdown。
- 因此主报告不是只在测反复权限切换带来的 steady-state toggle 成本。
- 该结果仍指向同一条 `mprotect()` PTE update path，所以应作为补充证据使用，不应另发
  成独立报告。

## 文件

- `single_protect_reproducer.c`：本 follow-up 的 standalone reproducer。
- `run_single_protect_reproducer.sh`：本目录内可直接使用的辅助脚本。
- `step-summary.csv`：逐 boot/run step 汇总。
- `aggregate-summary.csv`：按 kernel 聚合的汇总。
