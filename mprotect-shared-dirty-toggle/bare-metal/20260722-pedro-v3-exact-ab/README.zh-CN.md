# Pedro v3 当前环境精确 on/off A/B

## 结论

简短回答：**Pedro v3 没有改善这条限定 workload。** 在当前 i7-12700KF 裸机、
匹配的 `v7.1.3` 源码和构建环境中，完整 v3 相对两侧 no-v3 control 中点反而慢
**6.20%**。该指标越低越好。

| 点位 | v3 状态 | n | 均值 ns/page | SD | CV | 原始值范围 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| no-v3 A | off | 15 | 55.333 | 0.617 | 1.12% | 54–56 |
| Pedro v3 | on | 15 | 58.200 | 0.561 | 0.96% | 57–59 |
| no-v3 B | off | 15 | 54.267 | 0.594 | 1.09% | 53–55 |

两侧 no-v3 中点为 `54.800 ns/page`。no-v3 B 相对 no-v3 A 漂移
`-1.93%`；去掉每点第一轮后，v3 仍慢 `+6.18%`，control 漂移仍是
`-1.93%`。v3 的 15 个结果为 `57–59`，与两侧 30 个 control 的 `53–56`
没有重叠。

分项也把差异限定在 `mprotect()` 阶段，而不是后续 write-touch：

| 点位 | protect ns/page | restore ns/page | post-touch ns/page |
| --- | ---: | ---: | ---: |
| no-v3 A | 21 | 21 | 11.533 |
| Pedro v3 | 23 | 23 | 11.533 |
| no-v3 B | 21 | 21 | 11.733 |

因此，对这个特定的 64 MiB shared-dirty、4 KiB base-page 全区间 toggle
microbenchmark，当前精确结果不仅没有测到改善，还测到一个小但稳定的 slowdown。
这不等于 Pedro v3 对其他 CPU、工具链、folio 形状、`mprotect()` 模式或真实应用也会
变慢。

## Patch 当前性与精确源码身份

截至 2026-07-22，公开可找到的最新 revision 仍是 v3，没有找到 v4/v5。v3 已经通过
`40735a683bf8` 进入主线，从 `v7.1-rc1` 起就在正式 release 中；刷新到
`origin/master=248951ddc14d` 后，也没有看到更晚的 `mm/mprotect.c` 实现更新。结构化
审计见 `upstream-status.tsv`。

本实验从完全相同的正式 `v7.1.3` 源码归档构建两个内核：

- no-v3：只把 `mm/mprotect.c` 恢复成系列基点
  `19999e479c2a38672789e66b4830f43c645ca1f2` 的精确文件；
- full-v3：未修改的 `v7.1.3`，其 `mm/mprotect.c` 与系列终点
  `89e613bc0b2d6d4a18a09b161131ce4ca5c70f2a` 完全相同。

`3bc181c14363` 的直接 parent 是 `19999e479c2a`，`89e613bc0b2d` 的直接 parent 是
`3bc181c14363`；两片都只改 `mm/mprotect.c`。构建前源码树差异守卫也只报告这个文件。
所以这是 matched `v7.1.3` no-v3/full-v3 reconstruction，不会误称为一个新的上游
direct-parent commit A/B。

两个内核还使用了相同的：

- canonical config：
  `b1484511b7b7a3e3b1b8187018c2886ef939aa80e230be0aa3d7b74a202c3376`；
- GCC 15.2.0、ccache、Kbuild 时间戳/user/host/version；
- 外部模块签名 key 和 certificate；
- dynamic-preempt 构建，运行时 `preempt=none`；
- 等长 release string。

## Workload 与运行合同

- `64 MiB MAP_SHARED | MAP_ANONYMOUS` mapping；
- prefault 并写脏全部 `4 KiB` 页面；
- 反复整段 `PROT_READ -> PROT_READ|PROT_WRITE -> write-touch`；
- 每个 measured process 运行 1,000 轮，另有 10 轮内部 warm-up；
- 每点 3 个外部 warm-up process 和 15 个 measured process；
- 顺序为 `no-v3 A -> full-v3 -> no-v3 B`，每点 fresh boot；
- 固定 P-core CPU 2，governor/EPP 为 `performance`，Turbo 关闭。

45 个 measured process 全部通过：

```text
expected_match_ratio=100
unexpected_results=0
KernelPageSize=4 KiB
MMUPageSize=4 KiB
AnonHugePages=0
failed systemd units=0
```

第一次 no-v3 启动后，远程编排器曾因 shell 引号错误误报缺少
`preempt=none`，但实际 `/proc/cmdline` 含该 token。当时尚未计时；修正守卫后在同一
fresh boot 上重新执行了完整 60 秒 settle、内核身份检查、CPU profile 和全部计时流程。
这个点位的 boot uptime 因此比另外两点长，但两侧 control 漂移、drop-first 和无重叠
结果均已单独报告。

## 文件

- `summary.tsv`：三点主结果；
- `sensitivity.tsv`：control 中点、control 漂移和 drop-first；
- `component-summary.tsv`：protect、restore、post-touch 分项；
- `run-audit.tsv`、`selected-runs.tsv`：运行选择、fresh boot 和 gate；
- `runs/`：45 个 measured process 的原始输出、CPU profile 和环境；
- `source-manifests/`、`build-metadata/`、`install-metadata/`：源码、配置、构建与安装哈希；
- `upstream-status.tsv`：截至实验日的 patch 当前性审计；
- `prepare_build_install_pedro_v3_pair.sh`、`run_pedro_v3_ab_point.sh`、
  `run_remote_pedro_v3_sandwich.sh`、`analyze_pedro_v3_ab.sh`：可审计自动化。
