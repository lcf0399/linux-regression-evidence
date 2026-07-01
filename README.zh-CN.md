# Linux 回归证据仓库

这个仓库只保存经过后续复核后，仍然看起来值得上游讨论的 Linux 性能回归证据。

## 当前证据

- `mprotect-shared-dirty-toggle/`

  一条很窄的 Linux MM `mprotect()` workload：在 shared-dirty 4 KiB base-page
  mapping 上做权限修改。bare-metal 结果把 slowdown 缩小到 `v6.16 -> v6.17`
  release window；一个聚焦的 v6.17 single-PTE source probe 能把该 workload 拉回
  v6.16 的快区间；后续 single-protect follow-up 还说明，在准备好的 shared-dirty
  range 上，单次 `mprotect(PROT_READ)` 本身也能复现 slowdown。

  当前口径：这是 source-calibrated shared-dirty PTE workload，不是 generic
  `mprotect()` regression claim。

## 证据取舍

- 只保留整理后的 README、standalone reproducer、紧凑 CSV/JSON summary，以及理解
  claim 所需的小型 attribution probe。
- 不上传私有邮件草稿、失败 scratch logs、庞大的 raw runner workspace 或 local-only
  archive。
- 给上游邮件引用时，优先使用固定 commit 链接。
- 直接写清 workload scope 和 caveat；不要把窄 source-calibrated workload 包装成
  generic subsystem regression。
