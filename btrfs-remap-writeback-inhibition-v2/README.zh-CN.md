# Btrfs writeback-inhibition inline-buffer v2 验证

本目录保存对下列上游补丁的独立裸机验证：

```text
[PATCH v2] btrfs: replace writeback inhibition xarray with a fixed inline buffer
```

上游线程：

```text
Message-ID: <12d3c3f07b8610ca13b0f3f792d420541afb7b33.1782949130.git.loemra.dev@gmail.com>
https://lore.kernel.org/linux-btrfs/12d3c3f07b8610ca13b0f3f792d420541afb7b33.1782949130.git.loemra.dev%40gmail.com/
```

保存的 v2 patch clean apply 到冻结的 Linux 7.1.0
`f9a48549a15aa369d42cebc08a6a72b71a53d547`，SHA-256 为：

```text
5ec741be5a89d6dae0c0608cc036512770b55d6a49e9b576b4aa3115ebfdffd3
```

## 结果

主结果是 matched `control -> v2 -> control` 夹心实验。下表为全部轮次均值，单位
ns/op；负值表示 v2 更快。

| operation | control A | upstream v2 | control B | control 中点 | v2 相对中点 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 KiB `FICLONERANGE` | 2943.790 | 2159.123 | 2970.446 | 2957.118 | **-26.986%** |
| 4 KiB `FIDEDUPERANGE` | 3535.456 | 2762.835 | 3546.942 | 3541.199 | **-21.980%** |

两个 control 外端 drift 只有 clone `+0.905%`、dedupe `+0.325%`。每个点去掉
第 1 轮后，结论基本不变，分别为 `-27.178%` 和 `-22.128%`。

三个 timing 点共 90 行，全部为 `expected_match_ratio=100`、
`unexpected_results=0`。独立 direct-hit 还命中了全部必需函数：

| function | calls |
| --- | ---: |
| `btrfs_remap_file_range` | 2,000 |
| `btrfs_inhibit_eb_writeback` | 7,473 |
| `btrfs_uninhibit_all_eb_writeback` | 2,033 |

## 实验形状

- x86-64 真机：Intel Core i7-12700KF，32 GiB 内存；
- 每个点都在 1 GiB `/dev/ram0` brd 上重新创建 Btrfs；
- 固定一个 P-core 逻辑 CPU（`CPU 2`），`intel_pstate`，`performance` governor；
- `CONFIG_PREEMPT_DYNAMIC=y`，启动参数为 `preempt=none`；
- 每个内核点 15 个外部轮次；
- 每轮 10,000 次 4 KiB clone 和 10,000 次 4 KiB dedupe；
- control 与 v2 使用相同源码 commit、normalized config、GCC 15.2.0 工具链和
  可复现 Kbuild 身份。

## 口径和限制

这是 source-calibrated synthetic syscall micro-workload，不是真实应用 benchmark、
不是物理存储测试，也不泛化到所有 Btrfs 或 generic `remap_range` workload。结果只支持：
上游 v2 在这个 Btrfs 4 KiB clone/dedupe 形状上有效；它不能替代补丁作者的正确性测试和
更广泛文件系统测试。

上游报告和 v2 早于本次验证。本目录只是支持性测试证据，不主张首发现或补丁作者身份，
也不发布竞争性的本地实现。

## 目录内容

- `reproducer/`：standalone C workload 和带 guard 的 brd/Btrfs runner；
- `bare-metal/`：matched 裸机实验的紧凑 timing、构建身份、语义和 direct-hit 证据；
- `email/`：本地 reply 草稿和发送清单，已被 git 忽略，不属于公开证据包。
