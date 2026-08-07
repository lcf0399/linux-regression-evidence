# Reproducer

这里保留两个用途不同的 raw-UAPI 源文件：

- `io_uring_cancel_round.c` 是**产生正式结果的原样 1341 行源码**，完整保留实验契约、
  输出 schema、语义检查和其它 profile；
- `io_uring_cancel_miss_standalone.c` 是供维护者阅读的简短注释版，只保留主
  `A0_MISS` 路径，并自行完成建立环境、计时、清理和语义检查。

简版是对同一核心操作的独立交叉检查，不是产生公开表格数字的那个 binary。两份源码都
不依赖 liburing，默认固定到逻辑 CPU 2。相关的两个 profile 为：

- `a0_miss`：取消保证不存在的 key，并要求返回 `-ENOENT`；
- `a0_hit`：取消真实 pending poll key，并要求取消成功。

编译两个版本，并运行简版 reproducer：

```bash
make
./build/io_uring_cancel_miss_standalone
```

简版使用正式实验形状：3 轮 warm-up、15 轮 measured、每轮 64 个 batch、每个 batch
32 次尝试，并固定到 CPU 2。其输出只作为独立交叉检查；正式表格仍绑定原样源码。
另一次 fresh-boot parent/child/parent 实验中，简版复现了 child `+9.099%` 的 slowdown，
见 [`standalone-cross-check.tsv`](../bare-metal/standalone-cross-check.tsv)。

使用原样正式源码运行 semantic smoke：

```bash
./run_a0_once.sh a0_miss smoke /tmp/io-cancel-smoke
./run_a0_once.sh a0_hit smoke /tmp/io-cancel-smoke-hit
```

使用原样正式源码运行一个 3 轮 warm-up、15 轮 measured 的点：

```bash
./run_a0_once.sh a0_miss point /tmp/io-cancel-point
```

runner 拒绝覆盖已有结果，会校验全部输出行，并记录一份小型环境文件。正式跨内核比较仍
需要 fresh boot、相同内核配置和 binary、匹配的实际抢占模式、固定 governor/EPP/Turbo，
以及 parent/child/parent 顺序；standalone runner 不自动完成这些控制。
