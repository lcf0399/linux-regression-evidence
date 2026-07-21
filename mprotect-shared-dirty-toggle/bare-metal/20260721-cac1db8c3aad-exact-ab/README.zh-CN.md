# `cac1db8c3aad` 精确 A/B

本目录记录下面这个提交的裸机 direct-parent/child 实验：

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

## 结果

以两侧 parent 的均值中点为对照，该提交使 `iteration_ns_per_page`
增加 **39.77%**；该指标越低越好。

| 点位 | 提交 | n | 均值 ns/page | SD | CV | 原始值 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| parent A | `45199f715b74` | 15 | 38.133 | 0.743 | 1.95% | 37 38 38 38 39 37 39 37 38 39 38 38 39 39 38 |
| child | `cac1db8c3aad` | 15 | 53.533 | 0.516 | 0.96% | 53 53 54 53 53 54 54 54 53 54 53 53 54 54 54 |
| parent B | `45199f715b74` | 15 | 38.467 | 0.640 | 1.66% | 37 39 38 39 38 39 38 39 38 39 39 39 38 39 38 |

两个 parent 的中点是 `38.300 ns/page`；parent B 相对 parent A 只漂移
`+0.87%`。从每个点去掉第一轮后，child 仍慢 `+39.53%`，两个 parent 的漂移为
`+0.93%`。

45 个 measured process 全部通过语义和状态检查：

```text
expected_match_ratio=100
unexpected_results=0
KernelPageSize=4 KiB
MMUPageSize=4 KiB
AnonHugePages=0
```

分项指标方向一致：两侧 parent 的 protect 和 restore 均值都是 `13 ns/page`，
child 都是 `20 ns/page`；post-touch 均接近 `12 ns/page`。由于分项值被取整到整数
ns/page，上面的完整 iteration 指标仍是主结论。

因此，对这条限定 workload 而言，精确 parent/child 结果已经把测得的 slowdown
归因到 `cac1db8c3aad`。它不等于 generic `mprotect()` 或真实应用整体回归。

这里的绝对值不能直接与更早的 i7-14700 release-window 结果比较，因为硬件和内核构建
基线不同；commit 归因只来自本目录内部匹配的三启动夹心。

## 精确源码和构建契约

- parent：`45199f715b7455a2e4054dbc5dab0c3b65e2abc1`
- child：`cac1db8c3aad97d6ffb56ced8868d6cbbbd2bfbe`
- `45199f715b74` 是 `cac1db8c3aad` 的直接 parent
- 该提交只修改 `mm/mprotect.c`（`+113/-12`）
- 两份精确源码树的 kernel version 都是 `6.16.0-rc5`
- 源码按精确 commit ID 下载；归档和源码哈希在 `source-manifests/`
- 只去掉 `CONFIG_LOCALVERSION` 后，两份配置逐字节相同；canonical SHA-256 为
  `66064617009cb0b8d49edd1cd2fd1c7876965a3c1d0ec7304f0d54e0ce40c171`
- 两个内核使用相同的 GCC 15.2.0、Kbuild 时间戳、user、host、build version 和
  外部模块签名密钥
- 私有签名密钥、内核源码树和构建树没有进入公开证据

## Workload 和运行顺序

实验沿用 `../../reproducer/` 下的 standalone reproducer：

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping
- prefault 并写脏全部 4 KiB 页面
- 反复把整段 mapping 改成只读、恢复可写，再 write-touch 每一页
- 每个 measured process 执行 1,000 轮，内部 warm-up 10 轮
- 每个内核点先跑 3 个外部 warm-up process，再跑 15 个 measured process

实验机是 Intel Core i7-12700KF、32 GiB RAM。每个点都重新启动，固定到 P-core
CPU 2，governor 和 EPP 设为 `performance`，关闭 Turbo，并用运行时
`preempt=none`：

```text
parent A -> child -> parent B
```

三次启动的 boot ID 均不同，expected-kernel smoke check 全部通过，failed systemd
unit 都为 0。每个点结束后，runner 会恢复原来的 governor、EPP 和 Turbo 状态；夹心
完成后，机器也已回到发行版 rescue kernel。

## 文件

- `summary.tsv`：三点主结果。
- `sensitivity.tsv`：parent 中点和 drop-first-round 检查。
- `component-summary.tsv`：protect、restore、post-touch 分项均值。
- `runs/`：三点的 measured rows、reproducer 原始输出、CPU profile 和环境。
- `boot-smoke/`：已接受启动的内核身份和 smoke-test 记录。
- `source-manifests/`、`build-metadata/`、`install-metadata/`：精确源码、配置、
  构建产物和安装哈希。
- `prepare_build_install_exact_pair.sh`：带守卫的 exact-pair 构建/安装脚本。
- `run_exact_ab_point.sh`：带守卫的单点计时 runner。
