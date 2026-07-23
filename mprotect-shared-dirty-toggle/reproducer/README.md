# Standalone mprotect shared-dirty reproducers

This directory contains two standalone reproducers:

- `mprotect_shared_dirty_reproducer.c`: the primary 64 MiB, 4 KiB base-page
  workload;
- `mprotect_shared_pte_mapped_thp_reproducer.c`: the 2 MiB PTE-mapped
  large-folio reverse gate.

The primary workload is:

- `MAP_SHARED | MAP_ANONYMOUS` mapping
- write-prefault the whole range
- repeatedly toggle the full range with `mprotect(PROT_READ)`
- restore with `mprotect(PROT_READ | PROT_WRITE)`
- write-touch the range after each protection cycle

## Build And Run

Default helper invocation matches the current bare-metal evidence settings:

```sh
taskset -c 2 ./run_mprotect_shared_dirty_reproducer.sh
```

That expands to `MAPPING_MB=64`, `ITERATIONS=1000`, `WARMUP=10`, and
`EXTERNAL_ROUNDS=15`.

Equivalent manual invocation:

```sh
gcc -O2 -Wall -Wextra -o mprotect_shared_dirty_reproducer \
  mprotect_shared_dirty_reproducer.c

./mprotect_shared_dirty_reproducer \
  shared_dirty_full_toggle_64m 15 \
  --mapping-mb 64 \
  --iterations 1000 \
  --warmup 10
```

## Output

The main timing fields are:

- `protect_ns_per_page`: wall-clock ns per base page for `mprotect(PROT_READ)`
- `restore_ns_per_page`: wall-clock ns per base page for restoring write
  permission
- `post_touch_ns_per_page`: wall-clock ns per base page for the post-cycle
  write touch
- `iteration_ns_per_page`: `(protect + restore + post_touch)` wall-clock
  ns per base page

The `smaps_*` fields are a state-shape sanity check. For this workload, the
expected shape is a base-page shared mapping, not an anonymous THP path:

- `KernelPageSize = 4 kB`
- `MMUPageSize = 4 kB`
- `AnonHugePages = 0 kB`

This reproducer does not require the experiment framework. The bare-metal
evidence in the parent directory was collected by booting each target kernel
on the same physical machine and running this standalone reproducer.

## Large-folio reverse gate

The second program creates a 2 MiB shared folio, splits only its PMD mapping
into 512 PTE mappings, faults those PTEs back in outside the timed region, and
checks `/proc/self/pagemap` and `/proc/kpageflags` before reporting timing:

```sh
gcc -O2 -Wall -Wextra -Werror \
  -o /tmp/mprotect_shared_pte_mapped_thp_reproducer \
  mprotect_shared_pte_mapped_thp_reproducer.c

sudo env ITERATIONS=200 WARMUP=5 taskset -c 2 \
  /tmp/mprotect_shared_pte_mapped_thp_reproducer
```

It requires root for PFN visibility, a kernel with `MADV_COLLAPSE`, and a
shmem THP mode that permits collapse. It exits nonzero if the expected
one-head/511-tail PTE-mapped folio shape is not established.
