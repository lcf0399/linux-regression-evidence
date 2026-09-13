# 裸机结果

平台为 i7-12700KF、32 GiB RAM，单 caller 固定 CPU0、SCHED_OTHER，实际 full 抢占，
performance governor，Turbo 关闭。每份样本先预热 16 次，再测 128 对创建/删除；
每个条件每次启动九份样本，使用 CLOCK_MONOTONIC_RAW。干净计时不运行构建或探针。

## 精确提交：432 份样本、12 次独立启动

`exact-r1`、`exact-r2` 分别按以下顺序，每一点独立重启：

```text
base-A → guard-A → notify-A → notify-B → guard-B → base-B
```

base 为 `f917dc56060a10f401dd8ca46a1c5df237b35d84`，直接 child guard 为
`507d8ce13f5b91d5b4dca7bd4b4e4249e8021cca`，再下一 child notify 为
`eea5d2bb34ba11dccd9c53f392dc50cf060150a9`。这是 7.0-rc3 开发基线上的连续源码状态，
不是三个发行版本。GCC 15.2 匹配，内核配置只允许 LOCALVERSION 差异。

四种条件分别保留：原 cgroup workload 的 callback 关闭/开启，以及派生控制的 SCX 开启/关闭。
四种删除条件在两轮的 guard/base、notify/guard、notify/base 比较均通过：CV ≤3%、
绝对端点漂移 ≤2%、全部端点慢 ≥5%，以及去掉首份样本后的同样检查。

第二轮 SCX 关闭控制：guard/base +6.047%～+6.947%，notify/guard +37.332%～+38.081%，
notify/base +46.349%～+46.955%；第一轮累计增幅 +46.205%～+46.826%。
范围来自四种 A/B 端点比较，不是置信区间。

创建没有达到 5% 回归标准；首轮原 callback 关闭场景还存在 notify 端点漂移偏高。
这些数据全部保留，不因删除结果稳定而宣称所有指标稳定。

## root 复用：144 份样本、另四次启动

`root-reuse-r1`：notify-A → prototype-A → prototype-B → notify-B。
四种删除条件快约 4%～5%，节省约 0.38～0.49 µs；CV 和漂移合格，但没有一组在
全部端点及 drop-first 中均达到 5%。这是一轮镜像对照，不是两轮独立复验。
正确性边界见[原型说明](../attribution/README.zh-CN.md)。

## 固定主线：27 份样本、另三次启动

`mainline-r1`：v7.2-A → `2f0c1cf72f4682178506f513bbf015e591b1aa4a`
（7.3-rc2）→ v7.2-B，沿用同一个 SCX 关闭控制 binary。

| 点 | 删除，µs | 删除 CV | 创建，µs | 创建 CV |
| --- | ---: | ---: | ---: | ---: |
| v7.2-A | 9.919 | 0.750% | 18.608 | 7.044% |
| 固定 7.3-rc2 | 9.719 | 0.600% | 18.788 | 7.507% |
| v7.2-B | 9.953 | 0.505% | 18.767 | 7.227% |

删除改善 2.019% / 2.355%，旧端漂移 0.345%；drop-first 改善 2.039% / 2.459%。
创建 CV 超过 3%，表中只是参考值。所需功能与运行状态匹配，但新版 Kconfig 有增删，
不能称配置逐字节相同，也不能把改善归因于单一后续提交。

## 数据与复核

- `measured-rounds.tsv`：全部 603 份有顺序的运行均值，每行保留配对创建/删除。
  没有删除任何运行样本或长尾；不是全部逐操作原始日志。
- `point-summary.tsv`：各条件/启动/指标独立计算九份均值的中位数、总体 CV、去首份后的中位数。
- `comparison-summary.tsv`：端点效应范围、漂移、稳定性和原 5% 方向门槛；正数表示更慢。
- `identity.json`：已记录的源码/构建 SHA、保留的 v7.2 镜像身份、workload 身份和 19 个不同 boot。
- `mechanism.tsv`：48 个独立非计时精确源码窗口的同链计数，不是耗时拆分。
- `diagnostics.json`：root 复用、新版及 inode 返回值的独立诊断计数，均不是计时数据。
- `provenance.json`：原始记录与导出文件 SHA；其中来源名用于追溯，不是公开 raw 下载入口。

在本目录执行 `python3 -B verify.py`，可检查文件 SHA、603 份样本、134 条点统计、
58 条比较和启动身份。不会连接实验机、启动 workload 或代替私有原始 trace 审计。
本地导出时另核对四项实验各自的完整计时重放。

精确跟踪每次删除均为 26 个节点。guard 使属性写锁入口由 26 增至 52；notify 新增
26 次同链 inode 查找和 26 次包装层读锁，排除 IRQ。另一独立返回值 probe 在原场景
每次见 1 次命中/25 次 NULL，保持两个文件打开时为 3 次命中/23 次 NULL；20 个窗口
合计 520 次查找。NULL 不能证明可以安全跳过。新版检查仍命中相同删除结构。
