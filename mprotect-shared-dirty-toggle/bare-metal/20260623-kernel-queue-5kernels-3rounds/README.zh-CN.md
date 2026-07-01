# 2026-06-23 mprotect shared-dirty bare-metal kernel queue

这组结果来自新 bare-metal 节点，不使用 QEMU。测试方式是安装不同内核后通过
systemd 队列逐项重启，进入目标内核后运行同一个 standalone reproducer。

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
- `protect_ns_per_page` / `restore_ns_per_page` / `post_touch_ns_per_page` 是拆分阶段指标
- `expected_match_ratio=100` 且 `unexpected_results=0` 表示语义检查通过

队列为 5 个内核交错运行 3 轮，共 15 个正式 step：

1. `6.12.77-bm-6.12.77`
2. `6.19.9-bm-6.19.9`
3. `6.19.9-bm-6.19.9-pedro-v3`
4. `7.0.9-bm-7.0.9`
5. `7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57`

汇总见：

- `aggregate-summary.csv`
- `step-summary.csv`

当前汇总：

```text
kernel                                                        n  iteration_mean  iteration_cv_pct
6.12.77-bm-6.12.77                                           3          26.000             0.000
6.19.9-bm-6.19.9                                             3          37.000             0.000
6.19.9-bm-6.19.9-pedro-v3                                    3          39.000             0.000
7.0.9-bm-7.0.9                                               3          36.000             0.000
7.1.0-rc3-bm-mm-unstable-pedro-444fc9435e57                  3          39.000             0.000
```

注意：`logs/step-000_20260623T084217Z_6.12.77-bm-6.12.77.log` 是第一次启动队列时
遇到的目录权限失败记录，没有对应 `.done` step，也不计入 `step-summary.csv` /
`aggregate-summary.csv`。正式 step 从
`step-000_20260623T084831Z_6.12.77-bm-6.12.77.done` 开始。
