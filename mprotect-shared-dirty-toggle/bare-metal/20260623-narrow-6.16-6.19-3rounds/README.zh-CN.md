# 2026-06-23 mprotect release-window narrowing

这组结果来自新 bare-metal 节点，不使用 QEMU。测试方式是通过 systemd 队列启动不同
kernel，每个 kernel 运行同一个 `mprotect_shared_dirty_reproducer` standalone workload。

场景：

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`
- 每次 boot 后等待 60 秒再跑 benchmark

主指标：

- `iteration_ns_per_page`，越小越好
- `expected_match_ratio=100` 且 `unexpected_results=0` 表示语义检查通过

这轮队列为 5 个 kernel 交错运行 3 轮，共 15 个正式 step：

1. `6.16.0-bm-6.16`
2. `6.17.0-bm-6.17`
3. `6.18.0-bm-6.18`
4. `6.18.19-bm-6.18.19`
5. `6.19.9-bm-6.19.9`

汇总见：

- `aggregate-summary.csv`
- `step-summary.csv`

当前汇总：

```text
kernel                 n  iteration_mean  iteration_cv_pct  values
6.16.0-bm-6.16        3          25.000             0.000  25 25 25
6.17.0-bm-6.17        3          37.000             0.000  37 37 37
6.18.0-bm-6.18        3          38.000             0.000  38 38 38
6.18.19-bm-6.18.19    3          38.000             0.000  38 38 38
6.19.9-bm-6.19.9      3          36.667             1.286  37 36 37
```

所有 step 都是 `expected_match_ratio=100`、`unexpected_results=0`。

解读：

- `6.16` 仍在快区间，三轮都是 25 ns/page。
- `6.17` 开始进入慢区间，三轮都是 37 ns/page。
- `6.18`、`6.18.19`、`6.19.9` 保持慢区间。

因此这轮 bare-metal narrowing 把 slowdown window 从早先的 `6.16..6.19.9`
缩小到 `v6.16 -> v6.17` release window。它还不是 commit-level root cause；
下一步需要检查 `v6.16..v6.17` 中影响 `mprotect()` / PTE permission-change path 的
提交。

外部上下文：LKML 上已有一条独立的 mprotect regression 讨论，把类似的
`mprotect()` slowdown bisect 到 `cac1db8c3aad ("mm: optimize mprotect() by PTE
batching")`。本目录的 bare-metal narrowing 结果和这个方向一致，但本目录本身只记录
release-window 级别证据。

参考：

- https://lkml.iu.edu/2602.1/07208.html
