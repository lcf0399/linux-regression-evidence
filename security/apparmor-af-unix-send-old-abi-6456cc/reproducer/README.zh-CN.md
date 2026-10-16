# Reproducer

本目录同时保留生成报告数据的两份源码：

| 源码 | 用途 | 报告采用的 profile |
| --- | --- | --- |
| `af_unix_sendmmsg_standalone.c` | 简短、无额外依赖的复现器 | 直接 `sendmmsg()` |
| `io_uring_net_round.c` | 未修改的原始正式 workload | `u0` 直接 `sendmmsg()` 与 `s0` io_uring `IORING_OP_SEND` |

## 简短 standalone reproducer

`af_unix_sendmmsg_standalone.c` 是 328 行、无额外依赖的简短 reproducer，用于
复现主系统调用结果。它创建 AF_UNIX 数据报 socketpair，准备 32 条相互独立的
128-byte 数据报，只计时一次 `sendmmsg()`；随后在计时区外排空并校验全部消息。

构建和运行：

```bash
make
./af_unix_sendmmsg_standalone
```

默认参数与报告一致：CPU 2、3 轮 warm-up、15 轮 measured、每轮 65,536 条消息。
可用 `--cpu`、`--warmups`、`--rounds` 和 `--messages` 调整；`--messages` 必须为
32 的倍数。

每行输出消息数、batch 数、elapsed ns、ns/message 和 semantic-pass。程序还记录
kernel release、boot ID 和 `/proc/self/attr/current`。payload、长度、计数及空队列
检查全部是硬门。

该源码完全不初始化 io_uring，独立复现直接 `sendmmsg()` 结果。

## 原始正式 workload

`io_uring_net_round.c` 是大型正式 runner 实际使用的原始 1,130 行源码，本目录中的
副本未作修改，SHA-256 为
`e1b78e860713b4abc9e0ada7bafc9794172a304af9bc67a0d1db1c3d491b2216`。它直接使用
io_uring UAPI，不依赖 `liburing`。

AppArmor 精确源码差分表使用的两个 profile 可这样运行：

```bash
./io_uring_net_round --profile u0 --mode smoke --execute
./io_uring_net_round --profile s0 --mode smoke --execute

./io_uring_net_round --profile u0 --mode point --execute --output u0.tsv
./io_uring_net_round --profile s0 --mode point --execute --output s0.tsv
```

`u0` 在相同 AF_UNIX 数据报 socketpair 上计时批量 `sendmmsg()`；`s0` 使用相同队列
深度和 payload 大小计时 `IORING_OP_SEND`。程序固定 CPU 2，执行 3 轮 warm-up 和
15 轮 measured，并把对端排空和 payload 校验放在计时区外。输出文件已经存在时，
程序会拒绝覆盖。

源码还原样保留了其他正式 profile（`s1`、`r0`、`r1`、`m0` 和 `c0`）以维持来源
完整性，但 AppArmor 精确差分表没有使用它们。`describe` 输出中的
v6.12.95/v7.1.3 是原始 release-screen 合约；measured row 会记录实际运行的内核。

standalone 与 formal 百分比是两次独立复现。两者 runner 不同，实际抢占模式也分别
匹配但不同，因此不能把数字合并或相减。
