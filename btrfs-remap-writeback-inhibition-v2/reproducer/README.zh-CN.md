# Standalone reproducer

`remap_range_brd_micro.c` 是正式裸机实验实际使用的 C workload 源码。它准备 source
和 destination 文件，然后测量两种 4 KiB ioctl loop：

```text
FICLONERANGE
FIDEDUPERANGE
```

clone 场景统计 ioctl failure；dedupe 还要求返回 `FILE_DEDUPE_RANGE_SAME` 且实际
dedupe 字节数等于请求值。每条结果都会输出 `expected_match_ratio` 和
`unexpected_results`。

## 编译

```bash
cc -O2 -Wall -Wextra -std=gnu11 remap_range_brd_micro.c \
  -o remap_range_brd_micro
```

在已经挂载、支持 reflink 的文件系统上运行：

```bash
taskset -c 2 ./remap_range_brd_micro /mnt/test 10000 4096
```

三个参数依次是目录、操作数和 range 字节数。默认同时执行 clone 与 dedupe；设置
`REMAP_RANGE_SCENARIO=clone` 或 `REMAP_RANGE_SCENARIO=dedupe` 可只执行一种。

## 带 guard 的 brd/Btrfs runner

`run_remap_range_brd_micro_once.sh` 自动完成编译、格式化、挂载、多轮运行、语义检查和
summary。它拒绝路径不以 `/dev/ram` 开头的设备，也拒绝已经挂载的 brd；但它仍会销毁
所选 brd 设备上的内容。

依赖 `cc`、`btrfs-progs`、`util-linux`、容量足够的 brd 和 non-interactive sudo。
复现一个正式口径的实验点：

```bash
PIN_CPU=2 \
EXTERNAL_ROUNDS=15 \
ITERATIONS=10000 \
RANGE_BYTES=4096 \
BRD_DEV=/dev/ram0 \
BRD_SIZE_MB=1024 \
FSTYPES=btrfs \
./run_remap_range_brd_micro_once.sh
```

脚本不会主动修改 CPU frequency policy。做可比性能实验时，必须在所有被比较内核上统一
设置并记录 governor、抢占模式、CPU affinity 和其他系统控制。输出位于
`reproducer/out/`，已被 git 忽略。
