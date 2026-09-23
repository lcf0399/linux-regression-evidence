# Standalone reader

`rt_sysctl_read.c` is byte-identical to the source used for the published run.
It only opens the selected proc file read-only. It does not change sysctl,
power, boot, or tracing settings.

```sh
gcc -O2 -Wall -Wextra -Werror -std=gnu11 rt_sysctl_read.c -o /tmp/rt_sysctl_read
/tmp/rt_sysctl_read semantic period 32 2 1000000
KS_ALLOW_SYSCTL_TIMING=1 /tmp/rt_sysctl_read timing period 4096 2 1000000
KS_ALLOW_SYSCTL_TIMING=1 /tmp/rt_sysctl_read timing runtime 4096 2 950000
KS_ALLOW_SYSCTL_TIMING=1 /tmp/rt_sysctl_read timing rr 4096 2 100
```

Arguments are mode, case, reads, CPU, and expected numeric value. Check the
current values and allowed CPUs first. The examples reproduce the tested
values; they do not set them. A mismatch aborts rather than silently changing
the experiment. A ten-second watchdog bounds each invocation. `wall_ns/reads`
is the primary metric; `thread_cpu_ns/reads` is auxiliary. The JSON only
reports success after exact length/value and final affinity checks.

For the published protocol, run three warm-up and nine measured samples per
case at each independent boot. Rotate case order: period/runtime/rr, then
runtime/rr/period, then rr/period/runtime, repeating this order. Do not combine
probes with timing. Record and control configuration and runtime settings as
described in the main README. The included script for recomputing results does
not configure or reboot a machine.

## Optional write-check source (not the benchmark)

`sysctl_checks.c` is the separate source used for untimed correctness tests.
Unlike the reader, it **writes global RT settings** and is intended only for a
dedicated test machine with independent restoration. It requires root,
CPU 2 available, initial period/runtime 1000000/950000, and explicit
`KS_ALLOW_RT_WRITE_CHECKS=1`. Build it with the same compiler flags. Its modes
are `semantic` and `probe`; the latter exposes markers but does not attach a
tracer by itself. No example here runs it automatically.

Normal exits attempt restoration, but the watchdog or forced termination does
not guarantee it. The published run used an outer controller to restore saved
values and settings in a cleanup step. Do not run this helper on a shared or
production system without such protection.

The `tr_read_begin/end` and `tr_write_begin/end` markers bracket one syscall.
The independent probes count `sched_rt_handler`, `sched_rr_handler`,
`rebuild_sched_domains`, `partition_sched_domains`,
`dl_rebuild_rd_accounting`, `build_sched_domains`, `sched_dl_do_global`, and
scheduler-domain mutex operations only inside the matching process's marker
window. Per-operation records are included in `bare-metal/`; returned-value
checks and any restoration are outside that window.
