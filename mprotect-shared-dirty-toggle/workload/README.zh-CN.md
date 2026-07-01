# mprotect shared-dirty workload source

这个目录包含 `mprotect_shared_dirty_formal_refresh` profile 使用的 generated 用户态 workload source。

可报告 scenario 是：

- `shared_dirty_full_toggle_64m`
- anonymous shared 64 MiB mapping
- 在 protection change 前 prefault
- full-range `mprotect(PROT_READ)`，然后恢复为 `PROT_READ | PROT_WRITE`
- protection cycle 后 write-touch

包含这份源码是为了让 workload 语义可以被审计。这个仓库中的 formal timing evidence
由 experiment framework 以及 `../experiments/mprotect_shared_dirty_formal_refresh.toml`
profile 产生，不是由 standalone shell wrapper 产生。
