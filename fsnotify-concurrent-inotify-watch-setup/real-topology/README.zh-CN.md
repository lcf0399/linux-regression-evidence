# Stock `inotifywait` 拓扑 trace

这是路径/拓扑 gate，不是计时结果。

在 `7.1.3-bm-7.1.3` 上，8 个未经修改的 `inotifywait -m -r` 进程分别递归监控
Linux 7.1.3 源码树中互不重叠的 `arch`、`drivers`、`fs`、`include`、`kernel`、
`net`、`security` 和 `sound` 子树；所有子树都在同一个 ext4 superblock 上。

gate 结果：

- 8 个独立进程和 inotify fd；
- 4177 个目录 watch，与源码目录数完全相等；
- `fsnotify_add_mark_locked()` 4177 次；
- `fsnotify_inode_mark_connector` slab allocation 4177 次；
- `fsnotify_detach_connector_from_object()` 4177 次；
- 活动来自 9 个 CPU，PID 切换 1354 次，单个 1 ms 时间桶中最多同时出现 8 个 PID；
- 每个 watcher 都正确收到 create/close/delete 测试事件。

同时启动 8 个 watcher 是有意编排；本 trace 的任何 timing 都不进入性能 claim。

## 紧凑证据

- [`gate.env`](gate.env)：总 gate；
- [`watchers.tsv`](watchers.tsv)：逐进程、逐源码子树结果；
- [`trace-per-pid.tsv`](trace-per-pid.tsv)：逐进程内核路径计数；
- [`trace-topology.tsv`](trace-topology.tsv)：并发摘要；
- [`source-identity.tsv`](source-identity.tsv)：源码与内核身份。

## 重跑

runner 需要 `inotifywait`、`trace-cmd`、可直接使用的 `sudo`，并要求运行内核可 function-trace
两个 fsnotify 函数。脚本会在 8 个源码子树中各临时创建并删除一个 probe 文件，因此
`SOURCE_ROOT` 必须可写。

```bash
sudo apt install inotify-tools trace-cmd
SOURCE_ROOT=/path/to/linux-7.1.3 \
EXPECTED_KERNEL="$(uname -r)" \
./run_fsnotify_real_watcher_topology_once.sh
```

默认输出到 `real-topology/results/`。
