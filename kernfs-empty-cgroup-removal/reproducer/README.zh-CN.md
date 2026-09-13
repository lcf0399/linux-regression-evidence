# 实测 SCX 关闭控制的复现

`control.c`、`cg_shared.h`、`partial_scope.h` 与实测构建输入逐字节一致。
这是已测的派生叶子控制程序，没有为了整理仓库重新简化 workload。
历史 binary SHA-256：`ece6d9e0f3506079a4222826ccc566baaa022dddad68eb7b57177b7fb691abbe`。
源码 SHA 与构建版本见 `../bare-metal/identity.json`、`../bare-metal/provenance.json`。
重新构建后必须记录新 binary SHA，不能直接沿用历史值。

## 条件与构建

专用机器需要在 `/sys/fs/cgroup` 挂载 cgroup v2、CPU0 可用，并允许创建专用子树及启用
CPU 控制器。不得已有 SCX 调度器运行。原程序会检查 `/sys/kernel/sched_ext/state`，
因此即使本场景关闭 SCX，内核也需 CONFIG_SCHED_CLASS_EXT。
程序仍链接 libbpf，但关闭模式不会打开/加载 BPF 对象或挂载调度器。

实测构建为 GCC 15.2、GNU11、libbpf 1.6.3 和 pkg-config。备齐编译器与开发头文件后：

```sh
make
sha256sum build/control
```

Makefile 只构建用户态程序，不安装内核、不改配置、不启动实验。

## 单次运行

以下命令会创建并清理程序自己的 cgroup 子树。不要在生产机或其他实验计时期间运行：

```sh
sudo env -u CG_GRAPH -u CG_TRACE CG_SCX_DISABLED=1 ./build/control unused leaf 0 0 0 0 128 16
```

`unused` 是关闭模式不用的 BPF 文件名；后续参数选择叶子场景、零任务、无 callback/探针、
128 次正式操作和 16 次预热。循环分别计时 mkdir/rmdir；stat 与删除后 ENOENT 核对不计时。
父组/兄弟组、属性 FD 的准备及最终清理也不计时，程序不注册 inotify 监视项。
程序必须成功退出并输出 `restored: true`。

JSON 中 `first_ns`/`second_ns` 是创建/删除时间。每次运行只排除明确标记为 `warmup`
的行，用其余 128 行求均值，不删长尾。每个条件、每次启动运行九次形成样本分布。

此入口只复现关闭 SCX 的控制，不自动重现另外三种 SCX 条件或完整重启矩阵。
不测完整容器销毁，也不等待所有延迟回收结束。比较源码前仍需匹配内核功能、控制器、
实际抢占与频率设置。完整旧 runner 和 raw trace 不在这个紧凑包中。
