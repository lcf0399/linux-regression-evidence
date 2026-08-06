# 裸机结果

主结果是围绕
[`7029acd8a950`](https://github.com/torvalds/linux/commit/7029acd8a950393ee3a3d8e1a7ee1a9b77808a3b)
（`io_uring/rsrc: get rid of per-ring io_rsrc_node list`）的精确 direct-parent
对照。三次独立启动顺序为：

```text
e410ffca5886 parent A -> 7029acd8a950 child -> e410ffca5886 parent B
```

每点先运行 3 轮 warm-up，再运行 15 轮 measured；每轮以 QD 64 完成 4,096 次
fixed-file 安装。端到端 ns/install 均值为：

| 点 | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 102.476 | 0.705% |
| child | 114.441 | 0.289% |
| parent B | 102.576 | 0.340% |

child 相对两个 parent 控制中点慢 `11.621%`。每点删除首个 measured round 后为
`11.649%`，parent 漂移仅 `0.097%`。45 行 measured 数据全部通过 CQE、slot 分配、
sentinel、affinity 和 unexpected-result 检查。

三点使用相同 normalized config、GCC 15.2.0、Kbuild metadata、模块签名 key、等长
release string 和完全相同的 workload binary；实际运行抢占模式均为 `full`。紧凑身份和
逐点统计见 [`build-identity.tsv`](build-identity.tsv) 与
[`exact-ab-points.tsv`](exact-ab-points.tsv)。[`measured-rounds.tsv`](measured-rounds.tsv)
保留了 45 行入选的逐轮 timing，可复算 mean、CV、drop-first 和 semantic-pass；无需携带
完整 raw runner workspace。

## 辅助比较

另有两组独立 matched sandwich 得到相同方向：

- Linux 6.13 相对 Linux 6.12 控制中点慢 `10.747%`；
- Linux 7.1.3 相对 Linux 6.12.95 控制中点慢 `15.602%`。

它们只负责 release-level 复核，不能替代精确提交结果；所有正式点的实际运行模式也都是
`preempt=full`。三组比较集中在 [`result-summary.tsv`](result-summary.tsv)。

## 精简源码交叉验证

422 行的 F0-only 精简源码也在精确内核上按 parent A、child、parent B 三次独立启动：

| 点 | mean ns/install | CV |
| --- | ---: | ---: |
| parent A | 101.239 | 0.440% |
| child | 114.010 | 1.776% |
| parent B | 103.917 | 0.441% |

child 相对 parent 中点慢 `11.145%`，drop-first 为 `11.147%`；45 行数据全部通过
语义门，各点实际抢占模式均为 `full`。另一次不计时的 256-operation smoke trace 中，
`io_msg_ring()`、`io_msg_install_complete()` 和 `__io_fixed_fd_install()` 均精确命中
256 次。

两次 parent 控制漂移为 `2.645%`，略高于正式 `2%` 门槛，因此本结果只保留为紧凑的
方向性交叉验证，不晋级为第二组正式证据。数据见
[`standalone-cross-check.tsv`](standalone-cross-check.tsv)。

## 槽位数适用范围检查

在同一组精确内核上，再按 parent A、child、parent B 顺序分别测试 64、256、1,024 和
4,096 个 target slot，queue depth 始终为 64。child 相对匹配 parent 中点分别慢
`11.853%`、`8.717%`、`8.399%` 和 `11.889%`；180 行 measured 数据全部通过语义检查。

64 槽位的 child 与 256 槽位的 parent 控制噪声稍高，因此本轮只用于校准适用范围，
不替代正式的 4,096 槽位结果。它说明同方向信号在一批 64 次操作时已经存在，并非只有
超大 fixed-file table 才会出现；该幅度也没有随表大小单调增加。数据见
[`slot-gradient.tsv`](slot-gradient.tsv)。

## 非计时机制 trace

独立 parent/child trace 在两边各执行 128 次成功 SEND_FD 安装。
`io_msg_ring()`、`io_msg_install_complete()` 和 `__io_fixed_fd_install()` 都是 128 次；
固定文件安装内部嵌套的 `io_rsrc_node_alloc()` 则从 parent 的 0 次变为 child 的 128 次，
总调用数从 4 变为 129。

这证明 workload 直接命中了新增的逐安装 resource-node 路径，但不能据此断言全部时间差都
来自某一个 allocator 函数。计数见 [`mechanism-summary.tsv`](mechanism-summary.tsv)。

## 平台与范围

物理机为 Intel Core i7-12700KF、32 GiB RAM。单进程固定到 P-core 逻辑 CPU 2，governor
和 EPP 都为 `performance`，Turbo 关闭。

本证据只讨论经 `IORING_MSG_SEND_FD` 触发的 fixed-file registration/update；不声称普通
io_uring read/write 提交、应用性能或所有 fixed-file 更新场景都发生同样回归。
