# Standalone distinct/shared reproducer

`fsnotify_connector_topology.c` 是正式 P8 case 和 P6/P8/W16-SMT scaling 扩展使用的
原始语义 workload，只调用普通 inotify syscall 和 pthread。

聚焦 runner 会编译源码，交替 distinct/shared 的 matched round 顺序，检查语义与 CPU
affinity，并输出中值和变异系数。它不会自行调整 CPU governor、Turbo、抢占模式或后台服务；
比较两个内核前必须先设置并记录一致的稳定性能环境。

```bash
chmod +x run_p8_pair_once.sh summarize_p8.py

# 应选择 8 个不同物理核；下列 CPU 编号只适用于正式实验所用 i7-12700KF。
CPU_LIST=0,2,4,6,8,10,12,14 \
TEST_DIR=/dev/shm \
ITEMS=96 WARMUP_ROUNDS=2 ROUNDS=25 \
./run_p8_pair_once.sh
```

同一 runner 可用 `WORKERS=6 CPU_LIST=2,4,6,8,10,12` 复现同质 P6 点。辅助 W16-SMT
使用 `WORKERS=16 CPU_LIST=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15`；它混入 SMT sibling，
不能与只使用物理 P-core 的 P6/P8 混合解释。

选择 `CPU_LIST` 前先看 `lscpu -e=CPU,CORE,ONLINE`；复现主 P6/P8 形状时不要混入 SMT
sibling。默认 96 个独立 inotify instance 也要求 `fs.inotify.max_user_instances` 足够大。

主指标是所有 worker 的 add/remove 时间之和除以 watch 数
（`pair_worker_ns_per_watch`，越低越好）。paired `distinct/shared` 比值抵消了两种 inode
形状共有的大部分 inotify-instance、syscall 和 threading 开销。

正式实验源码 SHA-256：

```text
b452bda1dec37eff99667bd5ca678db92ad5620d823eecb546ca2644c3ef1cd4
```
