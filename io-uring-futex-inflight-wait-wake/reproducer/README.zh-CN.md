# Reproducer

`io_uring_futex_round.c` 是正式精确 A/B 未改动的 1,112 行源码；
`io_uring_futex_wait_wake_standalone.c` 是 338 行、只保留标量场景的审阅版本，已独立
复现相同方向和接近幅度。

两者都直接使用 io_uring UAPI，不依赖 liburing。

```sh
make
./build/io_uring_futex_wait_wake_standalone --smoke
./build/io_uring_futex_wait_wake_standalone
```

standalone 普通运行先做 3 轮 warm-up，再做 15 轮计时；每轮包含 512 个 cycle，每个
cycle 有 32 对 wait/wake。ring 建立、内存分配、CPU 固定和 warm-up 都在计时区外。

若出现异常 CQE、返回值、flag、timeout、重复/缺失 completion 或 CPU migration，程序
会以非零状态退出。部分容器或安全策略会禁用 `io_uring_setup()`，此时应在允许 io_uring
的宿主机内核运行。
