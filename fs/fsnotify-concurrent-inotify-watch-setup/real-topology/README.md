# Stock `inotifywait` topology trace

This is a path/topology gate, not a timing result.

On Linux `7.1.3-bm-7.1.3`, eight unmodified `inotifywait -m -r` processes
recursively watched these non-overlapping Linux 7.1.3 source subtrees:
`arch`, `drivers`, `fs`, `include`, `kernel`, `net`, `security`, and `sound`.
All subtrees were on the same ext4 superblock.

The gate passed with:

- 8 independent processes and inotify file descriptors;
- 4,177 directory watches, exactly matching the source directory count;
- 4,177 `fsnotify_add_mark_locked()` calls;
- 4,177 `fsnotify_inode_mark_connector` slab allocations;
- 4,177 `fsnotify_detach_connector_from_object()` calls;
- activity from nine CPUs, 1,354 PID transitions, and a maximum of eight
  watcher PIDs in one one-millisecond bucket;
- successful create/close/delete event delivery to every watcher.

Starting all eight watchers together was intentional. No timing from this
trace is used in the performance claim.

## Compact evidence

- [`gate.env`](gate.env): overall gate;
- [`watchers.tsv`](watchers.tsv): one row per process and subtree;
- [`trace-per-pid.tsv`](trace-per-pid.tsv): kernel-path counts per process;
- [`trace-topology.tsv`](trace-topology.tsv): concurrency summary;
- [`source-identity.tsv`](source-identity.tsv): source and kernel identity.

## Rerun

The runner requires `inotifywait`, `trace-cmd`, passwordless or cached `sudo`,
and a kernel exposing the two fsnotify functions to function tracing. It
temporarily creates and deletes one probe file in each selected source subtree,
so `SOURCE_ROOT` must be writable.

```bash
sudo apt install inotify-tools trace-cmd
SOURCE_ROOT=/path/to/linux-7.1.3 \
EXPECTED_KERNEL="$(uname -r)" \
./run_fsnotify_real_watcher_topology_once.sh
```

The script stores its output under `real-topology/results/` by default.
