# Uzair v3：首次填充、注册代价与同 ring 复用

实验日期：2026-09-10。作者原样 v3 使原 4,096 槽 first fill 相对未修改版快
**9.791%**、相对前两枚补丁快 **8.819%**。同 ring 的 4,096 文件批量移除→重装区间
分别快 **18.820% / 18.584%**；但注册开始至首次填充结束并未变便宜。

这是精确 **v6.18-rc4 同基线补丁验证**，不是 v7.2 实测、完整 ring 生命周期总成本，
也不是普通 READ/WRITE 性能结论。原 `7029acd8a950` 回归与历史诊断不覆盖。

## 身份与计时边界

| 角色 | 内容 | 完整本地测试 commit |
| --- | --- | --- |
| U | 未修改 public v6.18-rc4 | `6146a0f1dfae5d37442a9ddcba012add260bceb0` |
| B | 0001+0002，源码差异与既测 v2 相同 | `2359f858fa8db024958efc2eb7ac43a9002b44fe` |
| C | 原样完整 v3 | `3ad37b35866d99d62b992c53a1f45418f5fa76f2` |

完整 tree、原补丁字节 SHA、内核/工作负载身份和 12 个计时 boot ID 见
[`identity.json`](identity.json)。这里不分发私人邮件或作者原始补丁附件。

两次完成的计时矩阵各自使用六次独立启动：

```text
U-A → B-A → C-A → C-B → B-B → U-B
```

i7-12700KF，单工作进程固定 CPU2，在线 CPU0–19，微码 0x3e；GCC15.2、匹配的
canonical Kconfig 和构建控制，governor/EPP=performance、Turbo 关闭。共同命令行虽为
`preempt=none`，各点均另行切换并核实 **实际 dynamic preempt=full**。
干净计时 tracer=nop、events=0，所有机制 probe 独立运行。

- 原 F0 binary 未改：source ring 注册一个固定文件，每轮新建 target ring 并注册 4096
  sparse slots。计时 QD64 的 4096 次 SEND_FD 与 CQE 处理；注册、读回核验和销毁不计时。
  各 boot 为 3 轮预热、15 轮计量。
- 原注册辅助程序：4096/4096、4096/64、128/128（注册槽位/实际使用），分别测注册、
  首次填充和合计区间。3 个预热操作后，15 组×32 个新 ring。
- 新辅助程序：另立 identity，同样 3 个预热后 15 组×32 操作，六个场景一起运行。
  serial 不额外等待；paced 每次关闭 ring 后等待 50ms，等待不计时；reuse 保留同一
  ring/table，经 FILES_UPDATE(-1) 移除真实已安装槽位，再 SEND_FD 重装。
- 原 F0 保留 8MiB memlock，两个辅助程序均为进程级 64MiB；同一对照内 binary、
  UID 与 CPU 均匹配。各槽位检查 source/target CQE、真实返回槽位、fixed-file
  sentinel 读回和队列排空。
- 新 ring 合计不含创建、读回、注销/销毁与间隔；reuse 合计也不含初始注册/冷填充。
  **这些都不是完整生命周期总成本。**

## 原 F0

| 角色 | 中点 ns/install | 两端 CV | 自身漂移 |
| --- | ---: | --- | ---: |
| U | 119.423332 | 0.282% / 0.777% | +1.280% |
| B | 118.151131 | 0.930% / 1.920% | +0.158% |
| C | 107.731120 | 1.429% / 0.930% | -0.473% |

语义与稳定性全部通过；drop-first 为 C/U -9.766%、C/B -8.822%。
这是实际 v3，不是之前约 104ns 的诊断内核，104ns 也不是通过条件。

## 辅助计时与未过门结果

以下英文列名与 TSV 一致：registration=注册，first_fill/fill=首次填充或重装，
removal=移除；新 ring 的 interval 为注册开始→填充结束，reuse 为移除开始→重装结束。
单位都是每批操作微秒，越低越好；中点为同角色两次 boot 均值的平均。
NA 表示稳定性未过门，不表示没有代价/收益。

### 原注册辅助程序

| Scenario | Metric | U (us/batch) | B | C | C/U | Stability |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 4096/4096 | registration | 1.509 | 1.499 | 50.909 | NA | gate-fail |
| 4096/4096 | first_fill | 478.183 | 478.077 | 434.270 | -9.183% | pass |
| 4096/4096 | register_plus_fill | 479.708 | 479.591 | 485.197 | +1.144% | pass |
| 4096/64 | registration | 1.987 | 1.961 | 63.189 | NA | gate-fail |
| 4096/64 | first_fill | 8.196 | 8.488 | 7.509 | NA | gate-fail |
| 4096/64 | register_plus_fill | 10.199 | 10.465 | 70.716 | NA | gate-fail |
| 128/128 | registration | 0.379 | 0.389 | 2.436 | NA | gate-fail |
| 128/128 | first_fill | 15.994 | 16.063 | 13.973 | NA | gate-fail |
| 128/128 | register_plus_fill | 16.389 | 16.468 | 16.425 | NA | gate-fail |

### 新生命周期辅助程序

| Scenario | Metric | U (us/batch) | B | C | C/U | Stability |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| serial-4096-64 | registration | 2.079 | 1.985 | 64.637 | NA | gate-fail |
| serial-4096-64 | fill | 8.259 | 8.850 | 7.408 | NA | gate-fail |
| serial-4096-64 | interval | 10.354 | 10.851 | 72.062 | NA | gate-fail |
| paced-4096-64 | registration | 5.955 | 5.933 | 71.218 | NA | gate-fail |
| paced-4096-64 | fill | 14.379 | 15.232 | 13.164 | -8.451% | pass |
| paced-4096-64 | interval | 20.361 | 21.193 | 84.407 | NA | gate-fail |
| paced-4096-4096 | registration | 6.032 | 5.875 | 71.881 | +1091.558% | pass |
| paced-4096-4096 | fill | 512.329 | 514.762 | 454.967 | -11.196% | pass |
| paced-4096-4096 | interval | 518.387 | 520.663 | 526.873 | +1.637% | pass |
| paced-128-128 | registration | 2.080 | 1.948 | 5.066 | NA | gate-fail |
| paced-128-128 | fill | 22.536 | 23.492 | 20.174 | NA | gate-fail |
| paced-128-128 | interval | 24.643 | 25.469 | 25.268 | NA | gate-fail |
| reuse-4096-4096 | removal | 233.062 | 235.617 | 145.041 | -37.767% | pass |
| reuse-4096-4096 | fill | 479.372 | 474.751 | 433.305 | -9.610% | pass |
| reuse-4096-4096 | interval | 712.451 | 710.384 | 578.365 | -18.820% | pass |
| reuse-4096-64 | removal | 2.472 | 2.463 | 2.485 | +0.516% | pass |
| reuse-4096-64 | fill | 6.971 | 6.934 | 6.961 | -0.141% | pass |
| reuse-4096-64 | interval | 9.460 | 9.413 | 9.463 | +0.031% | pass |

- 大批次 reuse 三个指标均过门；合计最大组 CV 0.105%、端点漂移 0.596%，
  drop-first 同方向。64 文件的小批次基本持平，三个指标也过门。
- 原全量注册至填充合计 +1.144%，新 paced 全量合计 +1.637%，均过门：
  首填加快不等于加回注册仍有收益。
- paced 4096/64 的 C 两次 boot 合计均值为 83.350/85.465µs，
  漂移 +2.537% >2%。其注册和合计未过门，仅填充单项过门。
  原始大差距与闲置对象计数仍说明有提前付费的代价，不能因未过门便说没有成本；
  但不把原始均值比当作可靠的精确退化百分比。
- paced 128/128 三项均未过门，U 合计漂移 -2.580%。serial 4096/64 三项也未过门，
  合计最大组 CV 27.500%、漂移 9.187%。没有为追求过门而继续重跑或删长尾。
- paced 改变了节奏及缓存/调度状态，不与原 F0 的绝对时间混用，也不能由
  serial/paced 时间相减便归因异步销毁。

## 独立机制、内存和失败路径检查

完整紧凑记录见 [`mechanism-summary.json`](mechanism-summary.json)；没有把
跟踪耗时当成干净性能数据。

原 binary 在 U/B/C 都执行 73,985 次逻辑 `io_rsrc_node_alloc()`。C 的 19 个
target 填充 cycle 均不需要 cache-miss 分配；仅剩一次来自 source ring 普通 fixed-file
注册。逻辑节点处理没有消失。

三个角色各验证 128/128、4096/4096、4096/64、4096/0、8192/8192，CQE、读回、
cache 状态与最终释放均通过。8192 槽只预填充 4096，剩余 4096 次安装再执行
128 次完整 32-object bulk。

同 ring 独立计数的四次重装逐次一致：

| 每次重装 | U | B | C |
| --- | --- | --- | --- |
| 4096 文件 | 新分配 3968 个对象 | 124 次 bulk，共 3968 个对象 | 无底层新分配 |
| 4096 槽中只使用 64 文件 | 无底层新分配 | 无底层新分配 | 无底层新分配 |

每次大批次依然有 4096 次逻辑 node 调用。扩大缓存避免了后续反复进入分配器，不只是
提前支付第一次分配。大批次结束最终缓存释放数为 U/B 各 128、C 为 4096；计数完成后
各 cache 均为空、entries 指针清空。

C 的 4096/64：注册后缓存 4096 → 使用后 4032 → 注销后 4096 → 最终销毁时全部释放。
4032 是闲置管理对象，不是额外打开的文件；上限取决于注册容量，而不是未来使用量。
node payload=24bytes、allocator stride=32bytes，因此完整对象池占
128KiB allocator slots，另有 32KiB 指针数组。不是 RSS 或精确新增物理页，
也不保证 CPU cache 保持热；退出工作返回不表示所有 RCU 回调或 backing page 都立即回收。

15 个实际 cache C 代码的 allocator-stub 样例通过 ASan/UBSan/LeakSanitizer，
覆盖边界、已有对象、失败与清理；不是实际内核故障注入或穷尽并发正确性验证。
源码检查确认非 sparse 注册跳过新增 prefill 调用。

数组扩容成功、随后 bulk 失败时，已有 node 保留，但扩大的数组和 capacity 也会保留；
不能写成所有失败后 cache 完全不变。作者补丁没有私自改动。本基线 `init_clear=0`，
不能继续套用旧 `kzalloc` 诊断的“完整清零”描述。

独立生命周期 trace 中，各角色各连续场景第 2–16 轮开始时均有旧 ctx 退出工作未完成；
paced 则观察到 0/16。最长 close→exit-work 返回约 27.542ms，这是墙钟窗口，不是
CPU 一直忙于清理。实际 node-cache-free 与操作窗口的重叠，在连续 4096/4096
各仅 1/16，其他形状为 0/16。原 U 已有异步退出；这既未证明原高 CV 由它引起，
也不保证干净计时中的每个 50ms 都实现了同步销毁。

## 数据、复算与复现

- [`timing-summary.tsv`](timing-summary.tsv)：全部 28 项，13 项稳定性通过、15 项未过门；
  保留 CV、漂移、drop-first 与具体失败项；未过门的效应百分比写 NA。
- [`measured-groups.tsv`](measured-groups.tsv)：全部 900 个计量轮/组，可复算 mean、
  样本 CV、drop-first。F0 使用 `first_fill_ns_per_install`；辅助字段为每操作 ns，
  已按列出的 32 replicates 求平均。F0 的 fill/interval_ns 保留整批 4096 次安装耗时。
- [`individual-tail-summary.tsv`](individual-tail-summary.tsv)：新辅助实验全部 108 个
  逐点/逐指标单操作分布，含 P99、最大值；不宣称生产 P99 改善，组 CV 不是单操作 CV。
- [`identity.json`](identity.json)：完整来源、构建、binary、boot 与原始数据哈希。
- [原注册辅助源码](registration_cost_original.c)、
  [新生命周期辅助源码](registration_cost_lifecycle.c)：仅调整相对 include 路径，
  原始与整理版 SHA 均记录；不冒充字节完全相同的原 binary 或新性能结果。

[英文页](README.md#data-and-reproduction) 给出编译和调用命令；程序不会替你切内核、
重启或设置 governor/preempt，完整复现还需要匹配补丁、配置与上述运行条件。
不附带远程重启控制器。全部单操作样本、长尾和中止记录保留在原实验目录，
这里不复制庞大 raw trace、内核镜像或私人往来。两个已完成矩阵不混入中止结果。

本轮门槛不变：CV <3%、同角色两端漂移绝对值 <2%、drop-first 一致；
辅助指标按预先定义的 15 个组均值判断，不借分组冒充单次尾延迟稳定。
