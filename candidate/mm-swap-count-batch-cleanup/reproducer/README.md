# Reproducer

`shared_probe.c` is byte-identical to the measured program. It is standalone
and does not need the research workspace. Build and perform offline checks:

```sh
make check
```

Actual memory operations require an explicit opt-in on a dedicated Linux
test host with working anonymous swap, CPU 2 online, 4 KiB pages and pagemap
access for owned processes:

```sh
sudo env KS_SWAP_COUNT_RUN=1 ./build/shared_probe anchor
sudo env KS_SWAP_COUNT_RUN=1 ./build/shared_probe low
```

Do not run this on a production host. The program does not enable swap or tune
global THP, power or boot settings. Do not add memory pressure, use swapoff, or
repeat failed preparation until it passes. Failed state checks are not timing
samples. The program has a 40-second timeout and bounded preparation polling.

It prepares 33 private base pages with one MADV_PAGEOUT, selects 17 adjacent
swap slots in one cluster and discards the other 16 pages. `anchor` creates
14 children (15 holders including the parent); `low` creates seven. Three
children each unmap 16 pages while retaining the first page, all pinned to
CPU 2. Page-table identities and retained contents are checked.

Keep all output. `TIME stage=1..3 wall_ns=...` measures munmap return latency;
the sample metric is the arithmetic mean of the three values. Slot positions,
faults, context switches, and failures matter. This executable alone does not
prove internal function/branch reachability; separate probes supplied the
recorded state/entry checks before the [clean comparison](../bare-metal/README.md).

The [patch](../prototype/batch-cleanup.patch) applies to fixed MM commit
`2967f3ccdb5fae956f1b085370ec292bab5c7e25`, which already contains the author's
early-return optimization. Reported measurements are from that matched pair;
building this program does not reproduce or validate a kernel patch by itself.
