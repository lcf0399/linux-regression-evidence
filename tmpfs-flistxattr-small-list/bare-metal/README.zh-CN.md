# Bare-Metal 结果

这里的 timing 证据都来自 bare-metal。上游口径不使用本机/QEMU raw 数据。

## Release Window

场景：

```text
XATTR_SCENARIO=flistxattr_fd
TEST_DIR=/tmp
ITERATIONS=65536
EXTERNAL_ROUNDS=15
PIN_CPU=2
```

表格使用 skip-first-3 mean `ns_per_op`。

| label | kernel | mean ns/op | comparison |
| --- | --- | ---: | --- |
| v612a | 6.12.77-bm-6.12.77 | 162.250 | early baseline |
| v61819 | 6.18.19-bm-6.18.19 | 132.786 | fast range |
| v6199 | 6.19.9-bm-6.19.9 | 134.794 | fast range |
| v71 | 7.1.0-bm-7.1 | 220.360 | slow range |
| v612b | 6.12.77-bm-6.12.77 | 135.226 | old-version recheck |

关键变化：

| comparison | delta |
| --- | ---: |
| v6.19.9 vs v6.18.19 | +1.51% |
| v7.1 vs v6.19.9 | +63.48% |
| v7.1 vs v6.12 recheck | +62.96% |

## Exact Parent/Child A/B

对比 commit：

| role | commit | relevant state |
| --- | --- | --- |
| parent | b32c4a213698 | rhashtable infrastructure 已存在；tmpfs 仍走旧 rbtree path |
| child | 52b364fed6e1 | tmpfs 切到 lazy rhashtable-based simple xattrs |

场景：

```text
XATTR_SCENARIO=flistxattr_fd
filesystem=tmpfs
rounds=15
iterations=65536
pin_cpu=2
order=parent -> child -> parent
```

所有 run 均为 `expected_match_ratio=100`、`unexpected_results=0`。表格使用
skip-first-1 mean。

| target | mean ns/op |
| --- | ---: |
| parent A | 135.754 |
| child | 229.663 |
| parent B | 135.335 |

child 相对两个 parent run 的均值慢约 `+69.4%`。

跳过第 1 轮后的范围和标准差：

| target | min ns/op | max ns/op | stdev ns/op |
| --- | ---: | ---: | ---: |
| parent A | 134.451 | 140.153 | 1.742 |
| child | 217.758 | 239.498 | 7.126 |
| parent B | 134.273 | 138.098 | 1.048 |

parent 和 child 使用同一个 GCC 13.3 toolchain 构建，`.config` 除
`CONFIG_LOCALVERSION` 外一致。`CONFIG_TMPFS_XATTR=y`，`CONFIG_KASAN` 关闭，
配置中 `CONFIG_DEBUG_KERNEL=y` / `CONFIG_SLUB_DEBUG=y`。

## Count Gradient

场景：

```text
XATTR_SCENARIO=flistxattr_fd_count
XATTR_COUNT=1/4/16/64
rounds=9
iterations=65536
order=parent counts -> child counts -> parent counts
```

这个场景会检查返回 list 长度必须等于预置 xattr 名称的精确总长度。

| xattr count | parent A mean ns/op | child mean ns/op | parent B mean ns/op | child vs parent avg |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 156.102 | 242.886 | 156.872 | +55.2% |
| 4 | 185.109 | 273.756 | 181.206 | +49.5% |
| 16 | 312.894 | 378.211 | 315.962 | +20.3% |
| 64 | 885.222 | 934.356 | 870.759 | +6.4% |

这支持更窄的解释：该回归主要是 small-list fixed-cost 增加。

## Production-Like Config A/B

我还补跑了同一组 parent/child/parent A/B，并关闭了更可能放大 allocator 或遍历成本的
重 debug 选项：

```text
# CONFIG_SLUB_DEBUG is not set
# CONFIG_KASAN is not set
```

`CONFIG_DEBUG_KERNEL` 仍然保持启用，因为这套 config 里有 `CONFIG_EXPERT=y`，
而 upstream Kconfig 中 `EXPERT` 会 select `DEBUG_KERNEL`。如果为了关闭
`DEBUG_KERNEL` 而关闭 `EXPERT`，会引入更大范围的内核语义变化。因此这轮应解释为
matched `no SLUB_DEBUG / no KASAN` production-like A/B，而不是完整
`DEBUG_KERNEL=n` build。

场景：

```text
XATTR_SCENARIO=flistxattr_fd
filesystem=tmpfs
rounds=15
iterations=65536
pin_cpu=2
order=parent -> child -> parent
```

所有 run 均为 `expected_match_ratio=100`、`unexpected_results=0`。表格使用
skip-first-1 mean。

| target | mean ns/op | min ns/op | max ns/op | stdev ns/op |
| --- | ---: | ---: | ---: | ---: |
| parent A | 148.536 | 147.100 | 153.348 | 1.458 |
| child | 223.866 | 215.126 | 235.209 | 5.846 |
| parent B | 149.178 | 148.350 | 150.778 | 0.556 |

child 相对两个 parent run 的均值慢约 `+50.4%`：
`223.866 ns/op` vs `148.857 ns/op`，绝对差约 `75.0 ns/op`。

这说明关闭 `SLUB_DEBUG` / `KASAN` 后，small-list slowdown 仍然明显存在。
绝对差值比前一轮 debug-config A/B 小一些，但方向和量级仍然清楚。
