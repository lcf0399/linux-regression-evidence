# Linux 回归证据仓库

这个仓库只保存经过后续复核后，仍然看起来值得上游讨论的 Linux 性能回归证据。

它故意比本地研究目录小很多。已经被削弱、平台条件不满足、语义不成立，或者只剩
semantic smoke 价值的候选，不放进这个公开 evidence 仓库，而是保留在本地调查目录里。

## 当前证据

- `mprotect-shared-dirty-toggle/`

  一条很窄的 Linux MM `mprotect()` workload：在 shared-dirty 4 KiB base-page
  mapping 上做权限修改。bare-metal 结果把 slowdown 缩小到 `v6.16 -> v6.17`
  release window；一个聚焦的 v6.17 single-PTE source probe 能把该 workload 拉回
  v6.16 的快区间；后续 single-protect follow-up 还说明，在准备好的 shared-dirty
  range 上，单次 `mprotect(PROT_READ)` 本身也能复现 slowdown。

  当前口径：这是 source-calibrated shared-dirty PTE workload，不是 generic
  `mprotect()` regression claim。

## 不放进来的内容

早期 MM 候选，比如 `MADV_PAGEOUT`、`mincore()`、`migrate_pages()`、`mseal()`，
暂时不放进这个仓库，因为后续复核显示它们目前不是强上游回归报告。原因包括：
缺平台条件、QEMU/codegen sensitivity、语义不匹配，或者只剩 semantic-smoke 价值。

本地研究目录仍然可以保留这些历史，用来复盘方法和避免重复踩坑；但这个公开 evidence
目录应只保留当前真正可推进的回归证据。

## 证据取舍

- 只保留整理后的 README、standalone reproducer、紧凑 CSV/JSON summary，以及理解
  claim 所需的小型 attribution probe。
- 不上传私有邮件草稿、失败 scratch logs、庞大的 raw runner workspace 或 local-only
  archive。
- 给上游邮件引用时，优先使用固定 commit 链接。
- 直接写清 workload scope 和 caveat；不要把窄 source-calibrated workload 包装成
  generic subsystem regression。
