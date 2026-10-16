# Standalone Reproducer

`remap_range_brd_micro.c` is the exact C workload source used by the formal
bare-metal run.  It creates prepared source and destination files and times two
4 KiB ioctl loops:

```text
FICLONERANGE
FIDEDUPERANGE
```

The clone scenario counts ioctl failures.  The dedupe scenario additionally
requires `FILE_DEDUPE_RANGE_SAME` and the exact requested byte count.  Every
result line reports `expected_match_ratio` and `unexpected_results`.

## Build

```bash
cc -O2 -Wall -Wextra -std=gnu11 remap_range_brd_micro.c \
  -o remap_range_brd_micro
```

Run it on an already mounted reflink-capable filesystem:

```bash
taskset -c 2 ./remap_range_brd_micro /mnt/test 10000 4096
```

The arguments are `directory`, `operations`, and `range_bytes`.  The default
scenario is both clone and dedupe.  Set `REMAP_RANGE_SCENARIO=clone` or
`REMAP_RANGE_SCENARIO=dedupe` to select one.

## Guarded brd/Btrfs Runner

`run_remap_range_brd_micro_once.sh` automates compilation, formatting, mounting,
rounds, semantic checks, and summary generation.  It refuses any block device
whose path does not start with `/dev/ram` and refuses an already mounted brd
device.  It is nevertheless destructive to the selected brd device.

Prerequisites include `cc`, `btrfs-progs`, `util-linux`, a sufficiently large
brd device, and non-interactive sudo.  To reproduce one formal-style point:

```bash
PIN_CPU=2 \
EXTERNAL_ROUNDS=15 \
ITERATIONS=10000 \
RANGE_BYTES=4096 \
BRD_DEV=/dev/ram0 \
BRD_SIZE_MB=1024 \
FSTYPES=btrfs \
./run_remap_range_brd_micro_once.sh
```

The script does not change CPU frequency policy.  For comparable performance
measurements, set and record the governor, preemption mode, CPU affinity, and
other system controls consistently across all compared kernels.  Its local
output goes under `reproducer/out/`, which is ignored by git.
