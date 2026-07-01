# 2026-06-23 mprotect base-page probe

这组结果来自新 bare-metal 节点，不使用 QEMU。它是一个 attribution probe：
在 `6.19.9` 上临时加入 base-page single-PTE fast path，用来判断
`shared_dirty_full_toggle_64m` 的成本是否主要来自 base-page 路径进入
`mprotect_folio_pte_batch()` 前后的 folio/batching 形状。

这不是 upstream-ready patch，也不是 clean release kernel A/B。补丁归档在：

- `0001-mm-mprotect-probe-basepage-single-pte-fastpath.patch`

场景：

- `shared_dirty_full_toggle_64m`
- `mapping_mb=64`
- `pages=16384`
- `EXTERNAL_ROUNDS=9`
- `ITERATIONS=1000`
- `WARMUP=10`

主指标：

- `iteration_ns_per_page`，越小越好
- `protect_ns_per_page` / `restore_ns_per_page` / `post_touch_ns_per_page`
  是拆分阶段指标
- `expected_match_ratio=100` 且 `unexpected_results=0` 表示语义检查通过

结果：

```text
kernel                                n  iteration_mean  protect_mean  restore_mean  post_touch_mean
6.19.9-bm-6.19.9-basepage-probe       3          30.333        10.667        10.000            9.000
```

和同机 5-kernel 队列中的 clean kernel 对照：

```text
kernel                                n  iteration_mean  protect_mean  restore_mean  post_touch_mean
6.12.77-bm-6.12.77                    3          26.000         9.000         8.000            8.000
6.19.9-bm-6.19.9                      3          37.000        14.000        14.000            8.000
6.19.9-bm-6.19.9-pedro-v3             3          39.000        15.000        15.000            8.333
```

当前解释：probe 把 `6.19.9` 原版的 `iteration_ns_per_page` 从约 37 降到约 30，
说明 base-page resident PTE path 经过 folio/batching helper 的形状确实贡献了一部分
成本；但它没有完全回到 `6.12.77` 的约 26，所以剩余差异还需要 release-window 或更细
source attribution 继续定位。
