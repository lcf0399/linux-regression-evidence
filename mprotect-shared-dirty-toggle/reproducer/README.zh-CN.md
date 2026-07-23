# standalone mprotect shared-dirty reproducers

这个目录包含两个 standalone reproducer：

- `mprotect_shared_dirty_reproducer.c`：主要的 64 MiB、4 KiB base-page
  workload；
- `mprotect_shared_pte_mapped_thp_reproducer.c`：2 MiB PTE-mapped
  large-folio 反向门禁。

主要 workload 是：

- `MAP_SHARED | MAP_ANONYMOUS` mapping
- 先 write-prefault 整个 range
- 反复对完整 range 执行 `mprotect(PROT_READ)`
- 再恢复成 `mprotect(PROT_READ | PROT_WRITE)`
- 每轮 protection cycle 后 write-touch 整个 range

## 编译和运行

默认辅助脚本已经和当前 bare-metal evidence 使用的参数保持一致：

```sh
taskset -c 2 ./run_mprotect_shared_dirty_reproducer.sh
```

这等价于 `MAPPING_MB=64`、`ITERATIONS=1000`、`WARMUP=10`、
`EXTERNAL_ROUNDS=15`。

等价的手动运行方式是：

```sh
gcc -O2 -Wall -Wextra -o mprotect_shared_dirty_reproducer \
  mprotect_shared_dirty_reproducer.c

./mprotect_shared_dirty_reproducer \
  shared_dirty_full_toggle_64m 15 \
  --mapping-mb 64 \
  --iterations 1000 \
  --warmup 10
```

## 输出

主要 timing 字段是：

- `protect_ns_per_page`：`mprotect(PROT_READ)` 的每 base page wall-clock ns
- `restore_ns_per_page`：恢复写权限的每 base page wall-clock ns
- `post_touch_ns_per_page`：每轮后续 write-touch 的每 base page wall-clock ns
- `iteration_ns_per_page`：`protect + restore + post_touch` 的每 base page
  wall-clock ns

`smaps_*` 字段用于 state-shape sanity check。这个 workload 的预期状态是 base-page
shared mapping，而不是 anonymous THP 路径：

- `KernelPageSize = 4 kB`
- `MMUPageSize = 4 kB`
- `AnonHugePages = 0 kB`

这个 reproducer 不依赖 experiment framework。父目录中的 bare-metal evidence 是通过
在同一台物理机上启动不同目标内核，然后运行这个 standalone reproducer 收集的。

## large-folio 反向门禁

第二个程序创建一个 2 MiB shared folio，只把 PMD mapping 拆成 512 个 PTE mapping，
在计时区外重新 fault-in PTE，并通过 `/proc/self/pagemap` 与
`/proc/kpageflags` 检查页形状：

```sh
gcc -O2 -Wall -Wextra -Werror \
  -o /tmp/mprotect_shared_pte_mapped_thp_reproducer \
  mprotect_shared_pte_mapped_thp_reproducer.c

sudo env ITERATIONS=200 WARMUP=5 taskset -c 2 \
  /tmp/mprotect_shared_pte_mapped_thp_reproducer
```

它需要 root 才能读取 PFN，需要内核支持 `MADV_COLLAPSE`，并要求 shmem THP 模式允许
collapse。如果没有建立 one-head/511-tail 的 PTE-mapped folio 形状，程序会返回非零。
