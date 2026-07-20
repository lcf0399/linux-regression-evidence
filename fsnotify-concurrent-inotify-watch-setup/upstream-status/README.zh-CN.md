# 上游状态与 prior-art 审计

审计日期：2026-07-20。

## 结论

在已检查的最新正式版、当前主线、linux-next 和 fsnotify 维护者分支中，没有发现针对
connector 增删成本的源码级修复或等效优化。所有这些树在创建或摘除 inode connector 时，
仍执行
[`94bd01253c3d`](https://github.com/torvalds/linux/commit/94bd01253c3d5b1cd8955bdadeed24af02088094)
引入的 `list_lock` 与 `list_add()`/`list_del()`。

这个结论有意窄于“最新内核运行时仍慢相同百分比”。本证据包中的精确 slowdown 百分比来自
`6c790212c588` parent 与 `94bd01253c3d` child 的直接实验；没有用 `v7.2-rc4` 或 Linus
2026-07-19 tip 的运行结果声称后续内核仍有完全相同的百分比。

## 已检查 ref

所有 ref 均从 kernel.org 官方仓库抓取。精确对象 ID 和日期也记录在
[`refs.tsv`](refs.tsv) 中。

| 层级 | Ref | 精确 commit | 日期 |
| --- | --- | --- | --- |
| 最新正式版 | `v7.1.4` | `7a5cef0db4795d9d453a12e0f61b5b7634fc4d40` | 2026-07-18 |
| 主线候选版 | `v7.2-rc4` | `1590cf0329716306e948a8fc29f1d3ee87d3989f` | 2026-07-19 |
| Linus tip | `master` | `1590cf0329716306e948a8fc29f1d3ee87d3989f` | 2026-07-19 |
| linux-next | `next-20260717` | `0718283ab28bc3907e10b61a6b4be6fefa1cbb2f` | 2026-07-18 |
| linux-fs | `fsnotify` | `a3aa899823dda059ab88a58254f9a605e03ec275` | 2026-07-03 |
| linux-fs | `for_next` | `9fdf954edc4783314931f150413dc8afae18754c` | 2026-07-17 |
| linux-fs | `for_linus` | `5163e6ee1ea744d412fe516235bfd9cab15141dc` | 2026-06-12 |

版本边界另与 [`kernel.org`](https://www.kernel.org/) 交叉核对：审计当天最新 stable 为
`7.1.4`，mainline 为 `7.2-rc4`。

## 源码历史检查

审计逐个检查各 ref 的 `fs/notify/mark.c`，并用精确操作及 `inode_conn_list`、
`conns_list`、`list_lock` 符号检索历史。Linus 已检查 tip 上的相关代码仍是：

```c
spin_lock(&sbinfo->list_lock);
list_add(&iconn->conns_list, &sbinfo->inode_conn_list);
spin_unlock(&sbinfo->list_lock);

/* connector detach */
spin_lock(&sbinfo->list_lock);
list_del(&iconn->conns_list);
spin_unlock(&sbinfo->list_lock);
```

创建路径 808-810 行和摘除路径 825-827 行的 blame 仍指向 `94bd01253c3d`。精确源码可在
[Linus tree](https://github.com/torvalds/linux/blob/1590cf0329716306e948a8fc29f1d3ee87d3989f/fs/notify/mark.c#L808-L827)
核对。

关键符号历史中唯一命中的后续提交是
[`a05fc7edd988`](https://github.com/torvalds/linux/commit/a05fc7edd988c176491487ef0ae4dbf5f7a64cd7)
（`fsnotify: Use connector list for destroying inode marks`）。它在卸载时使用这张链表，
没有移除或降低 watch 建立/撤销时的 connector list 维护；因此它是 `94bd` 的依赖消费者，
不是本报告所述性能问题的修复。

## 邮件列表与维护者检查

审计按主题、commit ID 和精确 connector-list 符号，检索了官方 linux-fsdevel
public-inbox 从 2025-03-21 到 2026-07-20 的历史，以及 regressions 从 2021-04-06 到
2026-07-20 的完整历史。检索找到了原始系列和 review，但没有找到后续修复补丁，也没有找到
针对这项增删成本的既有回归报告。

原 review 曾讨论 lockless RCU 遍历；维护者回复解释，当时预期 inode notification mark
增删并不频繁。参见
[v3 cover](https://lore.kernel.org/linux-fsdevel/20260121135513.12008-1-jack@suse.cz/)、
[locking review](https://lore.kernel.org/linux-fsdevel/20260123-mengenlehre-wildhasen-46e47a6e7558@brauner/)
和[维护者回复](https://lore.kernel.org/linux-fsdevel/m5a3dyhvpnjhyjmxae2o2sd2azhynbrupmhzsy2fbgomhdcyow@imnv6ytjaxfi/)。

对精确源码 diff 运行当前 `scripts/get_maintainer.pl`，得到 Jan Kara、Amir Goldstein、
`linux-fsdevel@vger.kernel.org` 和 `linux-kernel@vger.kernel.org`。Christian Brauner
review 过 `94bd` 与 `a05fc7`，因此仍是相关 CC。

## 发送前决策

目前没有可供新 parent/fix A/B 验证的候选修复。上游草稿因此分别陈述精确 parent/child
结果和最新源码审计，并询问这项取舍是否值得优化。如果新 revision 或维护者分支修改这些
热路径操作，应把它当作新候选重新测试，不能只根据源码相似性推断运行时结果。
