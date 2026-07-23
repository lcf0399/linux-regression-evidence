# Pedro v3 当前环境精确裸机 A/B 计划

状态：实验已完成；完整结果见 `README.zh-CN.md`。full-v3 相对两侧 no-v3
control 中点慢 `+6.20%`，没有测到改善。

## 要回答的问题

在当前 i7-12700KF 裸机和正式 `v7.1.3` 源码基线上，Pedro 的 v3 两片
`mprotect` micro-optimization 是否改善现有的 64 MiB shared-dirty、4 KiB
base-page 全区间 protect/restore workload？

## 补丁身份

- 系列基点：`19999e479c2a38672789e66b4830f43c645ca1f2`
- 第 1 片：`3bc181c1436373e42220baaa0d8c9b45fa18afe1`
  (`mm/mprotect: move softleaf code out of the main function`)
- 第 2 片/系列终点：`89e613bc0b2d6d4a18a09b161131ce4ca5c70f2a`
  (`mm/mprotect: special-case small folios when applying permissions`)
- 当前正式基线：`v7.1.3`，commit
  `199c9959d3a9b53f346c221757fc7ac507fbac50`

### 截至 2026-07-22 的上游状态审计

- 公开邮件线程中能找到的最新 revision 仍是
  [Pedro v3](https://lore.kernel.org/all/20260402141628.3367596-1-pfalcato@suse.de/)；
  未找到 v4/v5。
- v3 随 `mm-stable-2026-04-18-02-14` 通过 merge commit
  `40735a683bf844a453d7a0f91e5e3daa0abc659b` 进入 Linus 主线。
- ancestry 检查确认它从 `v7.1-rc1` 起就在正式主线中，`v7.1`、`v7.1.3`
  以及后续 tag 也都包含它。
- 本地 Torvalds mirror 已刷新到 `origin/master`
  `248951ddc14de84de3910f9b13f51491a8cd91df`。该点对
  `mm/mprotect.c` 的最新两次修改仍是 v3 的 `2/2` 和 `1/2`，没有看到合入主线的
  后续实现更新。

所以目前没有新的 Pedro revision 需要改测；当前实验对照的是仍在最新主线中的完整
v3 实现，而不是已经过时的旧 patch。

本地审计确认，`v7.1.3:mm/mprotect.c` 与系列终点的文件 SHA-256 完全相同：

```text
79083647ff1f763c7e131d3ceba4bd9b313f9106e8d9a6b8a519acbf2a984906
```

系列基点文件 SHA-256 为：

```text
b189d27285e3506b090a6e3f242b9a9ec96ca913613ad084e745cdf51730f54f
```

因此实验采用同一个 `v7.1.3` 源码归档构建两个内核：

- `parent` / `base`：只把 `mm/mprotect.c` 精确恢复为系列前文件；
- `child` / `full`：未修改的 `v7.1.3`，包含完整 Pedro v3。

构建脚本会在配置前强制证明两个源码树只有 `mm/mprotect.c` 一个文件不同。
`base` 是精确 no-series 重构，而不是一个单独存在的上游 commit，因此最终报告会明确
使用 `matched v7.1.3 no-v3 reconstruction`，不会错误称为 direct parent commit。

## 计时合同

- 顺序：`no-v3 A -> Pedro v3 -> no-v3 B`；每点 fresh boot。
- 每点：3 个外部 warm-up process + 15 个 measured process。
- 每个 process：1,000 次迭代，另有 10 次内部 warm-up。
- CPU：固定 P-core CPU 2。
- governor/EPP：`performance`；Turbo 关闭。
- preemption：相同动态抢占构建，运行时 `preempt=none`。
- 主指标：`iteration_ns_per_page`，越低越好。
- 语义/状态 gate：返回语义正确、4 KiB Kernel/MMU page、无 THP。
- 稳定性：比较两侧 no-v3 control 漂移，并做 drop-first sensitivity。

实验完成后只更新本地证据和回复草稿；没有发送邮件，也没有同步任何仓库。
