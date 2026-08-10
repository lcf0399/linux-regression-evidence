# 裸机证据

- [`release-summary.tsv`](release-summary.tsv)：v6.12.95 与 v7.1.3 的夹心结果；
- [`exact-summary.tsv`](exact-summary.tsv)：`aa00f67adc2c -> a85f31052bce` 的精确
  parent/child 结果；
- `*-run-selection.tsv`：三点 kernel release、boot ID、requested/build/actual preempt
  和 workload 哈希；
- [`direct-hit.tsv`](direct-hit.tsv)：plain 与 inject 各 4,096 次命中
  `io_nop_prep()` 和 `io_nop()`。

正式 workload 固定 CPU 2、ring entries 128、QD64、每轮每个 profile 262,144 个 NOP，
3 轮预热、15 轮测量。trace 与正式计时分开执行。
