# 裸机精确 A/B 摘要

精确实验运行在 32 GiB 内存的 Intel Core i7-12700KF 机器上。benchmark 使用 8 个物理
P-core（`CPU 0,2,4,6,8,10,12,14`），scaling governor 与 energy-performance
preference 均设为 `performance`，关闭 Turbo，运行时 `preempt=none`。

三次独立启动构成以下夹心：

```text
6c790212c588 parent A -> 94bd01253c3d child -> 6c790212c588 parent B
```

每个点先运行 2 个 warm-up round，再运行 25 个正式 round。
[`exact-ab-summary.tsv`](exact-ab-summary.tsv) 中是中值。P8 absolute 指标是所有 worker
增删 watch 的时间总和除以 watch 数，不是 wall-clock latency；paired 指标是逐 round 的
distinct/shared 比值。

[`build-identity.tsv`](build-identity.tsv) 记录精确 commit 与 matched build/runtime
身份。parent/child 的 config、compiler、Kbuild metadata、module-signing key、抢占模式和
kernel-release 字符串长度均保持一致。

[`focused-mechanism-summary.tsv`](focused-mechanism-summary.tsv) 来自另一组五启动同 commit
诊断序列；其中修改过的内核只能用于归因，不能当作候选修复。

## scaling 扩展

[`scaling-extension-summary.tsv`](scaling-extension-summary.tsv) 记录第二组三启动 exact
夹心，包含 P6、P8 和辅助 W16-SMT。P6 使用 `CPU 2,4,6,8,10,12`，P8 使用
`CPU 0,2,4,6,8,10,12,14`；W16-SMT 使用逻辑 `CPU 0-15`，即每个 P-core 的两个 SMT
sibling。所有 topology 都是 96 watch、2 轮 warm-up、25 轮正式计时，并沿用上面的性能
合同和精确 parent/child 构建身份。
冻结协议 SHA-256 为
`7f7800d2d52135b15f1159be8edeb3ab12dfb5456d0a708705111b527d140822`。

P6、P8 同时通过 absolute/paired 的 `5%` signal、`5%` parent drift 和 `15%` CV gate。
W16-SMT absolute 同方向，但 child paired CV 为 `15.006954%`；实验后没有放宽门槛，因此
明确只把它作为辅助点。existing-mask 与 path-lookup 负控没有同向变化。

较早的 exact 运行中 P1、P4 distinct 均无材料性信号。比较各自具有完整夹心的两组运行，
可以支持“本机信号起点位于 P4 与 P6 之间”的窄结论，不能把它写成普适并发阈值。
