# Reproduce the unchanged-parameter cases

The [C source](sched_fair_control_v2_bench.c) is the unchanged measured benchmark. Its v2 filename is a benchmark revision, not a kernel-patch version. Only the following two profiles belong to this report.

Build with the measured compiler version when available:

```sh
gcc-15 -O2 -g0 -std=gnu11 -Wall -Wextra -Werror -pthread \
  sched_fair_control_v2_bench.c -o sched-fair-bench
./sched-fair-bench --profile setparam-current-v2 --cpu-a 2 \
  --ops 500000 --warmups 3 --rounds 15
./sched-fair-bench --profile setparam-fast-control-v2 --cpu-a 2 \
  --ops 500000 --warmups 3 --rounds 15
```

Use an available CPU; CPU 2 was used in the reported tests. Run as a non-root SCHED_OTHER/nice-0 task. The main profile restores default mode before timing; the control sets a custom 10 ms slice. Initialization is outside timing. Calls and final state are checked, and default mode and original affinity are restored at exit.

The program does not set the kernel's preemption mode or power policy. Keep actual runtime preemption, compiler/configuration, CPU and power settings matched across compared kernels. The reported comparisons used full preemption, performance governor/EPP and Turbo off, with no probes during timing. These commands do not recreate every correctness or PI check.
