# tmpfs flistxattr(fd) 小列表回归证据

这个目录保存一条很窄的 Linux FS 性能回归证据：

```text
tmpfs + user xattrs + flistxattr(fd) + 小数量 xattr
```

目前最强证据是围绕下面 commit 的 bare-metal parent/child A/B：

```text
52b364fed6e1 shmem: adapt to rhashtable-based simple_xattrs with lazy allocation
```

parent 已经有 rhashtable-based simple-xattr infrastructure，但 tmpfs 仍走旧
rbtree path。child 把 tmpfs 切到 lazy rhashtable-based simple-xattrs path。

单 xattr、跳过第一轮 warmup 后：

| kernel / state | mean ns/op |
| --- | ---: |
| parent, tmpfs old rbtree path A | 135.754 |
| child, tmpfs lazy rhashtable path | 229.663 |
| parent, tmpfs old rbtree path B | 135.335 |

child 相对两个 parent run 的均值慢约 `+69.4%`。所有 run 都是
`expected_match_ratio=100`、`unexpected_results=0`。

补跑的 production-like A/B 关闭了 `SLUB_DEBUG` 和 `KASAN`，child 仍然比
parent 平均慢约 `+50.4%`：`223.866 ns/op` vs `148.857 ns/op`。
`CONFIG_DEBUG_KERNEL` 仍为 y，因为这套 config 有 `CONFIG_EXPERT=y`，而
`EXPERT` 会 select `DEBUG_KERNEL`。

count-gradient 显示这个信号主要集中在小 xattr list，随着 xattr 数量增加会被枚举成本摊薄：

| xattr count | child vs parent average |
| ---: | ---: |
| 1 | +55.2% |
| 4 | +49.5% |
| 16 | +20.3% |
| 64 | +6.4% |

## 上游修复 follow-up

围绕 `1e7cd8a53b72 ("simpe_xattr: use per-sb cache")` 的精确裸机
parent/child 验证表明，该提交已完整消除本 workload 中测得的 slowdown：child 相对直接
parent 前后控制中值快 `35.93%`，相对 Linux 7.0.14 控制中值快 `4.21%`。不包含该提交的
Linux 7.1.3 stable 复核点相对 Linux 7.0.14 中值仍慢 `54.47%`。

原报告中的正式版点是 Linux 7.1.0（`7.1.0-bm-7.1`）；Linux 7.1.3 是后续增加的
stable-line 复核点，不是原报告点，也不是修复后内核。精简的六次启动验证证据见
`fix-validation/`。

## 范围

这是一条 small-list tmpfs `flistxattr(fd)` 报告。它不是说所有 xattr 操作、所有
tmpfs xattr workload，或者所有 rhashtable-backed xattr workload 都变慢。

本地调查里的 diagnostic source probe 指向 `shmem_listxattr()` /
`simple_xattr_list()` 以及 rhashtable walk 固定成本，但这些 probe 只是归因证据，
不是准备提交给上游的 patch 或 revert。

## 目录

- `reproducer/xattr_smoke.c`：xattr 实验使用的 standalone workload 源码。
- `bare-metal/`：可引用的 bare-metal release-window、exact A/B、
  production-like A/B、count-gradient 摘要。
- `fix-validation/`：上游 per-sb cache 修复的精确 parent/child 验证，以及更新后的
  Linux 7.1 stable 复核。
- `attribution/`：紧凑归因摘要，包括 bpftrace layering 和 diagnostic source-probe 结果。
