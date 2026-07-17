# `1e7cd8a53b72` per-superblock cache 修复验证

日期：2026-07-16 UTC

状态：在限定 microbenchmark 上通过裸机修复验证

## 问题

原报告测得 tmpfs small-list `flistxattr(fd)` slowdown。Jan Kara 询问下面提交之后是否仍能
观察到差异：

```text
1e7cd8a53b72 simpe_xattr: use per-sb cache
```

这里的标题拼写来自上游提交原文。

原报告中的正式版点是 Linux 7.1.0（`7.1.0-bm-7.1`）。本次 follow-up 另外加入 Linux
7.1.3 作为更新后的 7.1 stable 点。7.1.0 和 7.1.3 都不包含 `1e7cd8a53b72`；修复后点是
下面的精确 child commit。

## 源码坐标

```text
parent  076e5cef28e27febfc09b5f72544d2b857c75201
child   1e7cd8a53b72a58a44c4d282aed95f6ce0e76db0
```

二者是直接 parent/child。该提交后来通过 merge commit
[`ff8747aacaff`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux.git/+/ff8747aacaff8266dd751b8a8648fb728dcc3b21)
在 v7.2 merge window 进入 Linus 主线，但不在被测的 Linux 7.1 stable 点中。

## 方法

每个点独立启动，顺序为：

```text
7.0.14 A -> 7.1.3 -> parent A -> child -> parent B -> 7.0.14 B
```

workload 在 tmpfs 创建带一个 `user.*` xattr 的文件，反复调用
`flistxattr(fd, buffer, 8192)`。每个点运行 3 轮预热和 15 轮正式计时，每轮 1,048,576 次。
每条正式记录都要求返回列表长度符合预期且没有 unexpected result。

机器为 Intel Core i7-12700KF、32 GiB RAM，固定 P-core CPU 2；scaling governor 和
energy-performance preference 均为 `performance`，关闭 Turbo，内核命令行使用
`preempt=none`。parent/child 使用相同归一化配置、GCC 15.2.0 工具链和 Kbuild 元数据。

## 结果

主指标为 mean ns/op，越低越好。

| 点 | mean ns/op | CV |
| --- | ---: | ---: |
| Linux 7.0.14 A | 218.201800 | 0.518633% |
| Linux 7.1.3 | 336.140000 | 3.267516% |
| direct parent A | 327.037200 | 2.240271% |
| `1e7cd8a53b72` child | 208.446667 | 0.189207% |
| direct parent B | 323.640467 | 1.900889% |
| Linux 7.0.14 B | 217.007333 | 0.230280% |

使用前后控制中值：

| 比较 | 差值 |
| --- | ---: |
| Linux 7.1.3 vs Linux 7.0.14 | +54.472861% |
| direct parent vs Linux 7.0.14 | +49.509194% |
| child vs direct parent | -35.929362% |
| child vs Linux 7.0.14 | -4.208505% |

两个 Linux 7.0.14 控制漂移 `-0.547414%`，两个 direct-parent 控制漂移
`-1.038638%`。90 条正式记录全部通过语义检查。去掉每个点第一轮后，方向与差值基本不变。

## 解释

对这个限定的 tmpfs one-xattr `flistxattr(fd)` syscall microbenchmark，
`1e7cd8a53b72` 已完整消除报告中的 slowdown，并略微超过 Linux 7.0.14 基线。因此该
workload 没有需要继续 profile 的 post-fix 残余差距。

该结论不代表真实应用影响，也不泛化到其它文件系统、xattr 操作或 xattr-list 形状。
