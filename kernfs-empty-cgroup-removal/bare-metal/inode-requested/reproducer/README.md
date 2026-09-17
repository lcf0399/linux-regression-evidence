# Reproduce the scoped prototype comparison

Use a dedicated cgroup-v2 test host, not production. Kernel installation and
reboots are separate administrative steps; these files do not automate them.
The base and ordered A/B patches are in [the parent README](../README.md).
Keep compiler/configuration and runtime state matched. The unchanged original
harness requires CONFIG_SCHED_CLASS_EXT and libbpf even with sched_ext disabled;
it does not load a BPF scheduler in this mode. CPU0 must be available, with
permission to enable the CPU controller in the program's own subtree.

`make` builds four userspace programs. Original and cycle sources are reused
from this repository and checked by `../verify.py`; lookup and first-creation
sources are byte-identical exports. Rebuilding at a new path may change binary
hashes. Record those hashes rather than assuming the historical hashes match.

For clean timing, leave all tracing disabled. One invocation per case is:

```sh
sudo env -u CG_GRAPH -u CG_TRACE CG_SCX_DISABLED=1 ./build/original unused leaf 0 0 0 0 128 16
sudo env -u CG_GRAPH -u CG_TRACE CG_SCX_DISABLED=1 CG_CYCLE_PACE_MS=0 ./build/cycle unused leaf 0 0 0 0 32 16
sudo ./build/lookup 32 16 1024 none
```

All operate only on their own `kernel-study-*` subtree. Require successful
exit and cleanup/semantic fields before interpreting timing. Preserve JSON
output and all measured rows. Exclude the 16 explicitly marked warmup rows;
average each metric over the remaining 128 or 32 operations. For cached stat,
also divide the batch time by 1,024. Use nine invocations per case per boot.
The frozen order is original/cycle/lookup, then cycle/lookup/original, then
lookup/original/cycle, repeated three times. Boot A1/B1/B2/A2 independently,
settle 45 seconds after runtime preparation, and wait two seconds between
invocations outside timing. Do not pool boots or mix this with older matrices.

`cycle` directly records the mkdir-through-rmdir interval, including first
directory stat. `lookup` first queries one file in a new leaf, holds its FD,
runs cached stat calls, then verifies nlink after removal. Files rotate through
`cgroup.events`, `cgroup.procs`, `cgroup.threads`, and `cgroup.stat`.

For the separate **non-timing** first-creation check, CPUs 0, 2 and 4 must be
available with the recorded topology. Start `sudo ./build/first-creation`,
wait for its readiness JSON, then enter `G` and newline. It creates its own
subtree and runs 176 cases: 16 open-before-remove, 16 remove-before-open, and
144 races across nine offsets. It must report `completed: 176`, `cleanup: true`
and `pass: true`. Inspect any leftover named subtree after interruption;
do not recursively remove an unrelated cgroup. These spin-based offsets are
diagnostic controls, not a realistic workload or a performance measurement.

The helper alone checks FD semantics; it cannot certify fresh inode creation,
marker ordering, lock waits or notification delivery. Those claims require
the separate traced/notification checks documented in the parent README.
Raw traces and the lab's boot/probe orchestration are intentionally not shipped.
