# `6456ccbd2ff7` 后的 AppArmor AF_UNIX 发送 slowdown

这个 bundle 记录一条窄范围 AF_UNIX 数据报发送 slowdown，精确来源是
[`6456ccbd2ff7`](https://github.com/torvalds/linux/commit/6456ccbd2ff72814b3c1b2e2a3a2145a2ced858d)
（`apparmor: fix regression in fs based unix sockets when using old abi`）的源码增量。

主 reproducer 使用 `socketpair(AF_UNIX, SOCK_DGRAM)`，每次通过 `sendmmsg()`
发送 32 条 128-byte 消息；对端排空和内容校验均在计时区外。fresh-boot
parent/child/parent 夹心中，这个精确源码增量使 standalone workload 慢
`15.295%`：

| 点 | mean ns/message |
| --- | ---: |
| parent A | 343.382 |
| child | 395.831 |
| parent B | 343.259 |

parent 中点为 343.320 ns/message，parent 漂移 `-0.036%`，drop-first 为
`+15.289%`。45 个 measured row 全部通过 payload、长度、计数和空队列检查。
三点实际均为 `preempt=none`，固定在 CPU 2，governor/EPP 为 `performance`，
Turbo 关闭；三点的 `/proc/self/attr/current` 均为 `unconfined`。

使用原始大型实验源码的独立结果在每点主动切换到实际 `preempt=full`，同样复现：
直接 `sendmmsg()` 慢 `14.841%`，io_uring `IORING_OP_SEND` 慢 `17.953%`。
这两项是独立佐证，不能与 standalone 百分比混算。由于普通系统调用同样复现，
根因不能归到 `io_uring/net.c`。

另一组 v6.16.12 -> v6.17.13 release-endpoint `perf` 对照也给出了方向一致的结果：
`security_unix_may_send()` 的 children overhead 在两个旧 control 中均为
`0.27%`，在新点为 `6.77%`；新点的调用栈继续经过
`apparmor_unix_may_send()`、`aa_unix_peer_perm()` 和 `unix_peer_perm()`。
它支持把额外成本定位到 AppArmor 发送权限路径，但并非精确合成源码 pair，不能用来
继续拆分 standalone 的 `15.295%` delta 在各 helper 之间的占比。

原上游提交位于 AppArmor 系列中，不是适合直接测量的 mainline direct-parent pair。
因此实验在同一基线上构造受控 pair：parent 合入 AppArmor topic prefix 至
`50d56a1a366a`；child 只增加 `6456ccbd2ff7` 的精确两文件源码增量。child tree
与同一基线加 AppArmor prefix 至 `6456ccbd2ff7` 的 tree 完全一致。

该提交修复真实的旧 AppArmor policy ABI 正确性问题，本证据不建议回退。给上游的窄
问题是：能否保留该正确性，同时避免 unconfined AF_UNIX 数据报发送路径上测得的成本。

## 目录

- [`bare-metal/`](bare-metal/)：源码与运行身份、结果摘要和全部选中 measured row；
- [`reproducer/`](reproducer/)：简短 standalone 源码，以及生成 `u0`/`s0` 表的
  未修改原始正式 workload；
- [`upstream-status/`](upstream-status/)：发送前排重、修复审计和维护者路由。
