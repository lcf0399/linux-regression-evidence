# Reproducer

## 正式实验源码

`io_uring_msg_ring_round.c` 是正式实验使用的同一份 1,165 行源码，SHA-256 为
`21c99cd5cff34c21f815e7427e6f64997df864a6fb84f8c0cd26ede3d720e03c`。
源码包含三个 profile，但本证据只使用 `--profile f0`。

F0 先把一个 4 KiB memfd 注册到 source ring 的 fixed-file slot 0。每轮新建一个
target ring 和 4,096 槽 sparse fixed-file table，再以 QD 64 的批次通过
`IORING_OP_MSG_RING` / `IORING_MSG_SEND_FD` 填满这些空槽。计时区只覆盖 4,096 次
SEND_FD 及 source/target completion；逐槽读取并核对 sentinel 在计时结束后执行。

构建并运行 semantic smoke：

```sh
./run_f0_once.sh smoke /tmp/msg-ring-f0-smoke
```

运行一个完整 point（3 轮不计时 warm-up、15 轮 measured）：

```sh
ulimit -n 65536
./run_f0_once.sh point /tmp/msg-ring-f0-point
```

程序会固定到逻辑 CPU 2，但不会设置 governor、EPP、Turbo、抢占模式或重启机器；正式
跨内核比较必须由外层实验 harness 统一这些条件并保存记录。部分容器的 seccomp 会拒绝
`io_uring_setup(2)`，此时应在启用 io_uring 的主机或 VM 中执行。

主指标是每次成功 fixed-file 安装的端到端 ns/op。它包含用户态 SQE 准备、
`io_uring_enter()` 和 completion 处理，不包含 target table 创建、全槽验证和 teardown。

## F0 精简源码

`io_uring_msg_ring_f0_standalone.c` 是一份 422 行、带注释的审阅版本，SHA-256 为
`064d26197b1a9cc5e14bb7d648255c3403becb0d4278703b3f8b035a2398d1fe`。它只保留
F0 路径，直接使用 io_uring UAPI，不依赖 liburing，同时保留 source/target CQE、slot
唯一性、逐槽 sentinel、affinity、drop、overflow 与未完成操作检查。

```sh
make
./build/io_uring_msg_ring_f0_standalone --smoke
./build/io_uring_msg_ring_f0_standalone
```

精简版在同一组精确内核上的独立复核显示，child 相对 parent 中点慢 `11.145%`，与
完整源码的 `11.621%` 一致；全部语义门通过。不过两次 parent 控制漂移为 `2.645%`，
超过正式 `2%` 门槛，因此它只作为便于审阅的方向性交叉验证，不替换正式实验源码和
结果。

槽位数适用范围检查只把精确源码中的 `FD_OPS_PER_ROUND` 分别改为 64、256、1,024 或
4,096；queue depth 保持 64，并复用相同的精确内核顺序与语义门。未修改的 4,096 次
操作源码仍是主 reproducer。

## Node-cache cold/warm 诊断

`io_uring_msg_ring_node_cache_diag.c` 是 128 槽 cold/warm 诊断使用的 267 行源码，
SHA-256 为
`e6d41ddcb2d1109bea5e472e7d600305f56ffe368239ec7d40c2e76d4f8c2561`。它包含上面的
精简 standalone，以复用同一套 raw io_uring helper 与语义检查。选择 128 槽是为了匹配
v7.1.3 的 `IO_ALLOC_CACHE_MAX`；它不替代原 4,096 槽精确提交结果。

```sh
make
./build/io_uring_msg_ring_node_cache_diag --smoke
./build/io_uring_msg_ring_node_cache_diag --timing \
  --slots 128 --warmups 1 --pairs 15 --replicates 128
```

`trace_node_cache.sh` 采集独立的 PID-filtered function trace，用于推断 cache hit。它需要
root 权限访问 tracefs，且不能在 clean timing 时开启：

```sh
./trace_node_cache.sh ./build/io_uring_msg_ring_node_cache_diag cold /tmp/msg-ring-cold
./trace_node_cache.sh ./build/io_uring_msg_ring_node_cache_diag warm /tmp/msg-ring-warm
```
