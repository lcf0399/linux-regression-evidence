# Reproduce the tested sched_ext-disabled control

`control.c`, `cg_shared.h` and `partial_scope.h` are byte-identical to the
recorded control build inputs. This is the tested derived leaf control, not a
newly simplified reproducer. Its historical binary SHA-256 is
`ece6d9e0f3506079a4222826ccc566baaa022dddad68eb7b57177b7fb691abbe`.
Source hashes and GCC/libbpf versions are in `../bare-metal/original-regression/identity.json` and
`../bare-metal/original-regression/provenance.json`. Record a new binary hash after rebuilding;
do not assume it matches the historical artifact.

## Prerequisites and build

Use a dedicated host with cgroup v2 mounted at `/sys/fs/cgroup`, CPU0
available, permission to create a private subtree and enable its CPU
controller, and no active sched_ext scheduler. The unchanged harness checks
`/sys/kernel/sched_ext/state`; it requires CONFIG_SCHED_CLASS_EXT even when
the measured control has sched_ext disabled. It still links libbpf, but
disabled mode does not open/load a BPF object or attach a scheduler.

The recorded build used GCC 15.2, GNU11, libbpf 1.6.3 and pkg-config. With
the compiler and libbpf development headers available:

```sh
make
sha256sum build/control
```

The Makefile builds only the userspace control. It does not install a
kernel, change its configuration, or run a benchmark.

## One invocation

This command modifies only the program's private cgroup subtree. Do not run
it on a shared production host or alongside another benchmark:

```sh
sudo env -u CG_GRAPH -u CG_TRACE CG_SCX_DISABLED=1 ./build/control unused leaf 0 0 0 0 128 16
```

`unused` is an unused BPF filename in this mode; the remaining arguments
select the leaf scenario, no tasks, no callbacks/probe, 128 measured pairs
and 16 warmups. Each iteration times `mkdir`, performs untimed `stat`, times
`rmdir`, then checks `ENOENT` outside timing. Parent/sibling setup, attribute
FD setup and final cleanup are outside both timings. The program does not
register any inotify watches. It must exit successfully and report `restored: true`.

The JSON `first_ns`/`second_ns` fields are creation/removal latencies.
Exclude only rows explicitly marked `warmup` when taking each invocation's
128-operation mean; do not discard measured tails. Nine invocations give
the sample distribution for one arm/boot.

This invocation reproduces the disabled control, not the other three SCX
arms or the full fresh-boot matrix. It does not measure complete container
teardown or completion of all deferred reclamation. Required kernel features,
controller state, actual preemption and frequency settings must match before
using results for a source comparison. The original full harness and raw
traces are outside this compact bundle.
