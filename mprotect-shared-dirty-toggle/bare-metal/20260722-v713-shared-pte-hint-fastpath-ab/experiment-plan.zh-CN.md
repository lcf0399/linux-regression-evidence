# v7.1.3 共享单-PTE-hint fast path：实验计划

## 问题

既有 exact parent/child 与同提交机制消融表明，`cac1db8c3aad` 引入的
`vm_normal_folio()` 查找解释了原始回归约 44%，通用 commit/write/flush 路径解释约
43%。v7.1.3 已包含 Pedro Falcato 的 small-folio commit-path 优化，但仍在每个 present
PTE 上无条件执行 `vm_normal_page()`。

本轮只回答一个新问题：对于无需 page/folio 来决定写权限的共享、非 NUMA 映射，若架构
`pte_batch_hint()` 明确只能给出单 PTE，跳过这次查找能否在 v7.1.3 上恢复性能，同时保持
语义，并且会不会明显伤害 PTE-mapped 大 folio。

## 候选边界

候选仅在以下三个条件同时满足时令 `page = folio = NULL`、`nr_ptes = 1`：

1. 不是 `MM_CP_PROT_NUMA`；
2. VMA 是 `VM_SHARED`；
3. `pte_batch_hint(pte, oldpte) == 1`。

因此：

- 私有可写映射继续取得 page/folio，以检查 `PageAnonExclusive`；
- NUMA protection 继续检查 folio 是否适合 PROT_NUMA；
- arm64 contiguous-PTE 等返回大于 1 的架构提示继续走 folio batch；
- 共享路径的 writable 决策只依赖 VMA 与 PTE dirty 位，本身不读取 page/folio。

它仍不是可直接上游的修复。x86 的 `pte_batch_hint()` 恒为 1，所以 PTE-mapped 大 folio
也会逐 PTE 处理；这项性能取舍必须由反向门禁量化。

## 实验矩阵

在同一份 Linux v7.1.3 源码上构建两个等长 release-string 内核：

- `baseline`：未修改 v7.1.3；
- `candidate`：只应用本目录的单个候选补丁。

正式顺序为：

```text
baseline A -> candidate -> baseline B
```

每点全新启动，固定 P-core CPU 2，governor/EPP 为 `performance`，关闭 Turbo，
`preempt=none`。主 workload 与先前报告一致：64 MiB `MAP_SHARED|MAP_ANONYMOUS`、4 KiB
base pages、dirty 后反复 read-only/restore/write-touch；3 次 warm-up、15 次正式测量、每次
1,000 cycles。

主指标为 `iteration_ns_per_page`。candidate 对 baseline A/B 中点计算百分比变化，同时检查：

- 两个 baseline 漂移不超过 3%；
- 每点 CV 不超过 5%；
- 全部返回值检查通过；
- `KernelPageSize=4 kB`、`MMUPageSize=4 kB`、`AnonHugePages=0`；
- 删除各点首轮后结论不改变。

## 额外门禁

1. **运行时命中**：使用小型、非计时 bpftrace smoke，baseline 必须命中
   `vm_normal_page()`/`mprotect_folio_pte_batch()`。由于进程装载和 smaps 检查也会产生
   少量调用，candidate 不要求全进程计数为零；配对差值必须证明两个 1024-PTE
   protect/restore walk 对应的约 2048 次 lookup 与 batch-helper 调用消失。
2. **PTE-mapped 大 folio**：尝试在 shmem 上生成大 folio 后拆 PMD 映射为 PTE，必须用
   smaps 与 kpageflags 证明“4 KiB PTE 映射 + compound/THP folio”。若无法制造该状态，只能
   记录 unavailable，不能把候选标成上游就绪。
3. **环境恢复**：每个实验点后回到发行版 generic rescue；恢复 CPU profile 与
   `shmem_enabled`；终态不得残留 `BootNext`。

## 决策规则

- base-page candidate 至少快 10%、统计与语义门禁通过：说明该 fast path 值得继续；
- PTE-mapped 大 folio 恶化超过 10%：候选只能作为定位证据，不能直接建议上游；
- 任一语义/环境门禁失败：本轮无性能结论；
- 无论结果如何，都保留精确源码、配置、构建 hash、boot ID、原始行和分析脚本。

## 实际结果与终局决策

- base-page 正向门禁通过：candidate 相对 baseline A/B 中点快 `17.358%`，baseline
  漂移 `0.351%`，drop-first 仍为 `-17.323%`；
- 运行时命中通过：candidate 相对 baseline 恰好少 `2048` 次 `vm_normal_page()` 和
  `2048` 次 `mprotect_folio_pte_batch()`；
- PTE-mapped large-folio 反向门禁失败：candidate 慢 `65.797%`，drop-first 为
  `+65.839%`；
- 两组共 90 行正式测量的语义与页形状检查全部通过，最大 CV `1.096%`，两组 baseline
  漂移都远低于门限。

所以当前候选只保留为定位证据：它确认 shared base-page lookup 是真实成本，但
`pte_batch_hint() == 1` 无法在 x86 上安全排除 PTE-mapped large folio，不能作为上游修复。
完整说明见 `README.zh-CN.md`。
