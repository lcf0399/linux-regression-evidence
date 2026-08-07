# 复现程序

推荐上游先看 `io_uring_futex_waitv_wake_standalone.c`。它是 424 行 raw-UAPI 程序，
不依赖 liburing：

```sh
make
./io_uring_futex_waitv_wake_standalone
```

程序固定 CPU 2，运行三轮预热与 15 个 measured round，每轮输出 4,096 个
WAITV/wake pair 的 TSV。CQE、返回值、affinity、overflow 或残留 waiter 任一不符合预期
都会报错退出。

`io_uring_futex_round.c` 是精确实验使用的未改动多 profile 正式源码。本场景命令为：

```sh
cc -O2 -g -std=gnu11 -Wall -Wextra -Werror \
  -o io_uring_futex_round io_uring_futex_round.c
./io_uring_futex_round --profile v0_waitv_wake --mode point \
  --output result.tsv --execute
```

standalone SHA-256 为
`985f8acf23001bf3ef30c2ea3b781fe054e53c34b24b336426b4f26f52129408`。
源码开头的结果注释记录的是促成本次独立复跑的较早一次精确实验。为保持测试输入的
byte-for-byte 身份，源码没有改写；本次复跑数字以
[`../bare-metal/`](../bare-metal/) 为准。
