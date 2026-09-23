# RT sysctl 读取：Joseph Salisbury 补丁验证

在 v7.2 上，Joseph Salisbury 的补丁使重复读取 `sched_rt_period_us` 和
`sched_rt_runtime_us` 的耗时从约 8.57 µs 降至 0.264 µs，减少约 96.9%。
计时包含返回内容检查。这是对[已有补丁](https://patchew.org/linux/20260313183716.990792-1-joseph.salisbury@oracle.com/)
的裸机微基准验证，不是新设计的修复，也不是应用整体收益。

## 结果与方法

2026-09-23，完成一轮原版 A → 补丁 → 原版 B，每点独立重启。
均值单位为 ns/次读取，越小越好。

| 参数 | 原版 A | 补丁 | 原版 B | 相对原版中点耗时降低 |
| --- | ---: | ---: | ---: | ---: |
| `sched_rt_period_us` | 8547.294 | 264.575 | 8576.337 | 96.910% |
| `sched_rt_runtime_us` | 8574.240 | 263.690 | 8566.828 | 96.923% |
| `sched_rr_timeslice_ms` 对照 | 253.400 | 250.268 | 251.304 | 0.826% |

两项 RT 每次节省约 8.3 µs，最大组内 CV 为 0.891%，原版回切漂移为
0.339%／0.086%。去掉每点第一份正式样本后仍降低 96.913%／96.920%。
RR 对照不足预设 5% 门槛，不作为改善结论。未删离群点、未减去对照成本。

单读取进程固定 CPU 2，每项每点预热 3 份、测量 9 份，轮换三项的执行顺序。
每份对已打开的 FD 做 4096 次偏移为零的 `pread()`，每次核对返回长度和内容。
计时包括读取和核对，不包括打开／关闭、启动进程、设置环境和探针。
这不是排除了用户态检查的纯系统调用成本。
[逐样本数据](bare-metal/20260923-patch-pair/measurements.tsv)保留全部 108 份样本，含 27 份预热。

## 环境与源码身份

- i7-12700KF 裸机，20 个逻辑 CPU 全在线，单 NUMA、单 LLC。
- 两套均基于 v7.2：`8d3ae59288f1e7d58d76558a6ee96d533bc5019f`，
  GCC 15.2.0、GNU ld 2.46，补丁只修改 `kernel/sched/rt.c`。
- 实际配置除 `CONFIG_LOCALVERSION` 外一致；配置原文件均保留。实际完全抢占，
  performance governor/EPP，禁 Turbo，未启用 `CONFIG_RT_GROUP_SCHED`。
- RT period/runtime 均为 1000000/950000 µs，RR 为 100 ms。
- 三点用同一读取程序二进制；计时阶段 tracing 关闭。

[身份与冻结参数](bare-metal/20260923-patch-pair/identity.json)记录构建摘要、环境和启动时间。
两份配置的架构标题注释不同，不是配置项不同。较早的准备尝试曾因此停止，尚未重启或计时；
修正比较规则后复用同一次构建，只完成上述一轮矩阵，没有择优重跑。
实验后已恢复设置、原启动状态及 generic 内核。

## 补丁和正确性边界

补丁作者为 Joseph Salisbury，原邮件发表于 2026-03-13。
[本轮版本](attribution/joseph-v7.2-context.patch)仅适配上下文，因为 v7.2 已没有
`sched_rt_do_global()`；设置本地标志、成功更新后置位、按标志调用重建三处逻辑未改变。

独立非计时探针只包住每次目标系统调用，不包含回读、恢复。
原版 RT 读取都会进入重建入口、分区维护和 DL 带宽重新计账；补丁版均跳过。
但原版读取时 `build_sched_domains()` 也始终为零，即复用了现有调度域，
不是每次读取都重新创建整张域。调度域锁的取得／释放从两对降至一对，初始锁仍在。

成功写入（含相同值）仍执行全局更新与重建。有效修改、非法文本、越界值及无效
period/runtime 组合的返回值、回读和失败后恢复行为一致。补丁版失败写入不再触发重建。
RR 对照没有进入 RT 重建。未观察到内核告警或 taint。
完整的[读取探针](bare-metal/20260923-patch-pair/read-probes.tsv)和
[写入检查](bare-metal/20260923-patch-pair/write-checks.tsv)均保留。

未覆盖并发写入、CPU 热插拔、活跃 SCHED_DEADLINE 任务下的准入失败、多 NUMA、
`CONFIG_RT_GROUP_SCHED=y`，也未测成功写入性能和监控应用整体收益。
不能称全面安全证明，也没有把 v6.12.95→v7.2 的全部增量精确归因到某一个提交。

## 使用材料

- [复现说明](reproducer/README.md)：独立读取程序、编译和运行方式，写入工具另列并注明风险。
- 在此目录运行 `python3 bare-metal/20260923-patch-pair/recompute.py`，
  可从逐样本数据复算均值、CV、漂移和去首份结果，并与保存的汇总核对；不会运行实验。
- [上游状态](upstream-status/README.md)：原线程及有日期的主线核对，验证有效不等于已经合入。
