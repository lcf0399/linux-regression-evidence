# Standalone distinct/shared reproducer

`fsnotify_connector_topology.c` is the exact semantic workload source used for
the reported P8 case and the P6/P8/W16-SMT scaling extension. It uses only
ordinary inotify syscalls and pthreads.

The focused runner compiles the source, alternates matched distinct/shared
round order, enforces semantic and CPU-affinity checks, and reports medians and
coefficients of variation. It does not change the CPU governor, Turbo state,
preemption mode, or background services; set and record a stable performance
environment before comparing kernels.

```bash
chmod +x run_p8_pair_once.sh summarize_p8.py

# Choose eight different physical cores on the test system. The list below is
# specific to the i7-12700KF machine used for the published result.
CPU_LIST=0,2,4,6,8,10,12,14 \
TEST_DIR=/dev/shm \
ITEMS=96 WARMUP_ROUNDS=2 ROUNDS=25 \
./run_p8_pair_once.sh
```

The same runner can reproduce the homogeneous P6 point by setting
`WORKERS=6 CPU_LIST=2,4,6,8,10,12`. The W16-SMT auxiliary point used
`WORKERS=16 CPU_LIST=0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15`; it includes SMT
siblings and must not be mixed with the physical-core-only P6/P8 results.

Inspect `lscpu -e=CPU,CORE,ONLINE` before choosing `CPU_LIST`; do not mix SMT
siblings when reproducing the primary P6/P8 shapes. The default 96 independent
inotify instances also require a sufficiently high
`fs.inotify.max_user_instances` limit.

The primary metric is the sum of per-worker add and remove time divided by the
number of watches (`pair_worker_ns_per_watch`, lower is better). The paired
`distinct/shared` ratio controls for most inotify-instance, syscall, and
threading overhead that is common to both inode shapes.

Source SHA-256 used in the original experiments:

```text
b452bda1dec37eff99667bd5ca678db92ad5620d823eecb546ca2644c3ef1cd4
```
