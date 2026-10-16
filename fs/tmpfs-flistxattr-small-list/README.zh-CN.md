# tmpfs flistxattr(fd) 小列表回归证据

本线于2026-10-10明确结题，不再排队跟进或追加实验。对应修复已在主线，
本项目的报告／验证证据继续保留；专用实验内核及可重建产物按结题范围退役。

时间线（北京时间）：修复 `1e7cd8a53b72` 经
[`ff8747aacaff`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux.git/+/ff8747aacaff8266dd751b8a8648fb728dcc3b21)
于2026-06-15进入Linus主线，早于我们的7月6日报告。原报告测试的7.1.0以及后续7.1.3
均不包含该修复；Jan于7月9日要求复测，我们于7月17日反馈精确父子版本的裸机验证。
因此，这是对已发布7.1回归的独立复现和对既有主线修复的验证，不是报告后才写成／合入的修复。

2026-10-10核对正式版本：该修复是
[v7.2正式标签](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux.git/+/refs/tags/v7.2)
（`8d3ae59288f1`，北京时间2026-08-17发布）的祖先，不是v7.1的祖先。
v7.2源码仍保留每superblock缓存和链表枚举实现。因此，首次包含它的主线正式版是v7.2。
这不等于已经核实它是否／何时回移到更旧的stable分支，也没有新增性能测量。

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
