# 裸机证据与方法

按问题分为[原始回归及 TCP](original-regression/README.md)、
[旧专用 slab／32 批量](dedicated-slab32/README.md)、
[共享 slab／128 原型](shared-slab128/README.md)、
[历史 v3](uzair-v3-prefill/README.zh-CN.md)和[早期诊断](history/README.md)。
完整方法和可供上游直接阅读的说明见[英文页](README.md)。

最新三组目录各用一个 `timing.tsv` 保存所有试次，一个 `results.tsv` 保存全部分项和
去首组结果，一个 `provenance.json` 保存身份、非计时计数及数据摘要。未通过稳定性的项
同样保留，不会被删除或算成“无影响”。旧 v3 的完整数据及有价值的早期 TSV 仍保留。

统一使用 i7-12700KF、32 GiB RAM、CPU 2、实际 full 抢占、performance 与关闭 turbo；
同一比较内构建与运行条件匹配。先做独立非计时语义/命中检查，再控制 A→变体→控制 B
独立启动计时。每点 3 次预热，管理测试 15×32 次、TCP 15×4 次；统计单位为组均值。
完整和去首组均须 CV<3%、控制回切漂移绝对值<2%；不为过门反复重跑。

管理测试的注册＋首填不含建 ring、销毁或业务。TCP 包含建立连接、SEND_FD、实际收发和
关闭；4 连接逐批建 ring，64/256 复用 ring 并整批更换连接。这是单进程单 CPU 的本机
合成测试，不证明真实应用经常这样更换连接。复用模式一次性建 ring/销毁单列，不能将
每行重复保存的一次性字段逐行相加。50 ms 间隔在计时外，也不证明所有异步回收已结束。

在 target 目录执行：

```sh
python3 -B reproducer/verify_followups.py
```

脚本仅读取公开材料，重新计算 26,586 行的 156 个分项，核对稳定性判断和 108 个库存快照。
不访问实验机，不改变系统，不重新计时。复现程序单独放在 `reproducer/`，没有复制多套。
