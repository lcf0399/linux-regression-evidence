# Reproducer

`io_uring_memmap_round.c` 是产生本证据包正式数字的 **原样 1,081 行 raw-UAPI
workload**，SHA-256 为
`fc8135fcb64de09c5396437608dabee9a54b01d5c0a649720728ea3f29250127`。
它保留完整输出 schema、语义检查和其他对照 profile；当前回归 claim 只使用
`l0_standard_lifecycle`。

构建：

```bash
make
```

不执行系统调用即可查看固定参数：

```bash
./run_memmap.sh --profile l0_standard_lifecycle --phase describe
```

运行一次小型语义检查：

```bash
MEMMAP_EXECUTE_ACK=YES \
  ./run_memmap.sh --profile l0_standard_lifecycle --phase smoke \
  --out-dir /tmp/io-memmap-smoke
```

运行一个与正式形状相同的 3-warm-up/15-measured point：

```bash
MEMMAP_EXECUTE_ACK=YES MEMMAP_TIMING_ACK=YES \
MEMMAP_REQUIRED_REQUESTED_PREEMPT=none \
MEMMAP_REQUIRED_BUILD_PREEMPT=dynamic \
MEMMAP_REQUIRED_ACTUAL_PREEMPT=full \
  ./run_memmap.sh --profile l0_standard_lifecycle --phase point \
  --out-dir /tmp/io-memmap-point
```

runner 会核对实际 preempt、CPU 2 governor/EPP、Turbo 和 tracing 状态，并调用
validator。单次运行不能替代正式的 fresh-boot parent/child/parent 对照；跨内核结论还要求
相同 config、编译工具链、Kbuild 元数据、workload binary 和机器策略。

本目录没有另造一个简版程序，因此不会把未经相同实验验证的源码误写成正式 reproducer。
