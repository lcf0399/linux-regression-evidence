# Reproducing the finite-limit zswap check

`zswap_case.c` and its included helper file `peak_state.c` are byte-identical to
the measured sources. The latter also retains an unused historical main function;
the preprocessor renames it and the zswap executable does not call it.
`zswap_case_unlimited1.c` is the separately measured control variant, not a
replacement for the original program. Its [source diff](unlimited1-source.diff)
only adds the single-worker unlimited selector and updates the selftest count.
`zswap_case_statwait.c` is the separately measured eight-worker repeat variant:
it adds bounded preparation-stat convergence without changing the timed worker.
It includes the same `peak_state.c`; compile it with the same flags, for example
`gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror zswap_case_statwait.c -o build/zswap_case_statwait`.

```sh
make
make selftest
python3 verify_results.py
```

The measured compiler was GCC 15.2.0. The `--selftest` command only checks the
non-runtime entry; it does not validate kernel behavior, probes or performance.

## Before any privileged run

Use a dedicated, recoverable test host, not a production or shared machine.
Require cgroup v2 memory/pids controllers, swap and zswap support, debugfs mounted
at `/sys/kernel/debug`, and permission to read pagemap and zswap counters. Match
the [builds, full configurations and runtime settings](../bare-metal/README.md).
The unchanged program assumes 4 KiB pages and CPU IDs through 18; verify topology
before running. A different CPU layout needs an explicitly separate test contract.

Require at least 4 GiB available RAM, initially unused swap, and zero
`/sys/kernel/debug/zswap/stored_pages`. There must be no competing zswap workload:
the program checks a global counter. Enable zswap with LZO / zsmalloc and the
recorded pool settings before measurement, saving and restoring any changed
settings. Do not disable active swap to manufacture an empty state.

Prepare a new empty owned cgroup whose absolute name begins
`/sys/fs/cgroup/ks-memcontrol-rest-` and has no further slash. Its parent must
already delegate memory and pids. Set its `memory.max=268435456`,
`memory.swap.max=134217728`, `pids.max=32`, and enable `+memory +pids` in its
`cgroup.subtree_control`. Keep the controller outside that group. The program
creates the leaf `g0`, joins its workers, and selects a 64 MiB or unlimited zswap
limit itself. This package does not silently alter host-wide configuration.

For example, with a separately prepared empty owned root:

```sh
sudo ./build/zswap_case /sys/fs/cgroup/ks-memcontrol-rest-zswap-review finite1
```

Valid cases are `finite1`, `finite8`, and `unlimited8`. Total allocation remains
32 MiB. The separate `build/zswap_case_unlimited1` also accepts `unlimited1`;
only that new case was used in the follow-up batch. A successful run prints checked `PREP`, `STATE`, `WORKER`, `CLEANUP` and
`RESULT` records. The primary metric is `RESULT pageout_cpu_ns`, summed across
workers; `restore_verify_cpu_ns` is separate. Neither is whole-program CPU.
Whole-program CPU in the evidence was measured externally using child-process
resource accounting, including waited descendants, not by summing these phases.

Keep an external watchdog and an independent recovery connection. The program
also sets a 25-second alarm and child parent-death signals; these do not replace
external cleanup. On any failure, stop sampling, terminate only owned workers,
wait for them, remove only the owned leaf/root after they are empty, and verify
zswap contents and temporary settings are restored. Never accept an exit code
alone in place of the state/data/cleanup checks. Do not delete unrelated cgroups
or loop until a state failure disappears.

For a matched series, first do independent state/path checks on both kernels,
then remove probes. Follow the fixed parent/child/parent order, two warmups and
nine measured repetitions per case per boot. Preserve every observation and
the return point. Do not run the historical parent as a proposed production fix.

For the later counter evaluation, use baseline/derivative/baseline on the
[recorded MM base](../bare-metal/counter-mm-pair/README.md), with the same fixed
sample counts. Keep its three-case batch and the new single-worker unlimited
batch separate. The [November 6 eight-worker repeat](../bare-metal/counter-eight-repeat/README.md)
is also separate, not extra samples in the old matrix.
`verify_results.py` recomputes all four datasets and checks
identities and retained failures; it does not execute or revalidate kernel races.
