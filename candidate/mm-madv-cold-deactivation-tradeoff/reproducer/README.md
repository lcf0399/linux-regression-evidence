# Measured programs and reproduction boundaries

The C sources are byte-for-byte copies of the measured programs, not newly
simplified replacements. Each subdirectory includes its required local helper
source/header. No internal Python framework is needed to compile them.

```sh
gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror anonymous/vector_remote.c -o anonymous-cold
gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror file/workload.c -o file-cold
./anonymous-cold --selftest
./file-cold --selftest
python3 verify_results.py
```

The self-tests only check constants/clocks. They do not establish page state,
path hits or performance. The verifier checks the retained formal outputs and
arithmetic without changing kernel settings or running the workloads.

## Anonymous protocol

Use a dedicated Linux test machine with CPU2 online, 4 KiB pages, MGLRU enabled
and support for `pidfd_open` / `process_madvise`. Match the two configurations
and relevant settings in [environment.json](../measurements/environment.json).
The program initially requires root, drops to UID/GID1000 and retains only
`CAP_SYS_NICE` for this interface; it clears that capability at exit.

```sh
sudo env KS_ALLOW_REMOTE_VECTOR=1 ./anonymous-cold vector-self-one
sudo env KS_ALLOW_REMOTE_VECTOR=1 ./anonymous-cold vector-self-many
```

These are self-targeting cases; the additional child/denial selectors in the
unchanged source are not part of this exact-commit dataset. Each invocation
performs two 16 MiB rounds. Phase30 CPU/wall totals are divided by two. Mapping,
fresh filling, state queries and validation are outside the operation window but
inside the complete-program metric. Each mapping is checked for base-page form,
residency and content retention. The process has a 256 MiB address-space limit
and a 25-second alarm. Do not apply these commands to another user's process.

## File-state protocol

This preparation is more specific. It is not sufficient merely to invoke
`MADV_COLD` twice or sleep for a fixed time.

An administrator must create a **new, empty, owned** cgroup v2 child whose name
starts with `/sys/fs/cgroup/ks-cold-private-20261020-`, under a memory-enabled
parent. Set `memory.max=201326592`, `memory.swap.max=0`, `pids.max=16`; arrange
delegated write access to this group's `memory.reclaim`. Prepare an owned
directory on a regular file-backed filesystem with at least 32 MiB free.
Do not substitute tmpfs for the file-cache protocol.

Launch the worker as UID/GID1000, already joined to that group, pinned to CPU2,
with exactly these variables and selector:

```text
KS_ALLOW_COLD_FILE=1
KS_OWNED_FILE_DIR=<owned regular-filesystem directory>
KS_OWNED_CGROUP=<the new private cgroup path>
./file-cold cold-file-private-reclaim
```

The program creates and unlinks two private 16 MiB files, completes writeback,
and uses ordinary COLD and one `memory.reclaim` request. The target must retain
4096 resident pages, a 16 MiB base-page mapping and correct contents. The request
is 4096 bytes, not a bound on the kernel's actual reclaimed amount.

Phase10 is fresh target COLD; phase11 is a one-page control call to drain the
tail. Phase30 is oldest-state target COLD; phase31 is its drain. Report the target
interface and target-plus-drain separately. Whole-worker CPU/wall includes
private file preparation, the reclaim request, full validation and internal
unmapping/closing, but not the administrator's cgroup creation/removal.

Before timing each tested kernel, use matching non-timing observers to confirm:

- phase10's 4096 target folios are not in the oldest generation;
- phase30's 4096 folios are in the oldest generation and remain resident;
- the parent still enqueues/moves them; the child skips the oldest-state work;
- any queued tail is accounted for through the adjacent drain phase.

The userspace success flag alone does **not** prove the internal generation.
Observers require calibration to the actual kernel symbols and ABI; this bundle
does not provide a supposedly universal probe. If the state cannot be confirmed,
stop before treating results as a comparable oldest-generation experiment.
Do not add pressure until a favorable result appears.

After exit, verify the cgroup is empty, has no OOM/limit-event change, and remove
only that owned group/directory. Restore captured temporary settings. Never use
global cache dropping, arbitrary cgroup deletion or swap changes as implicit
preparation.

## Original measured schedule

Both batches use parent A → child → parent B, freshly booted points. The anonymous
batch has one separate path check, one warmup and nine measured invocations per
case per boot. The file batch has one semantic check, one probed check, two
warmups and nine measured invocations per boot. Do not time with probes attached.

Configs differ only in their release suffixes. Kernel and worker compiler was
GCC15.2.0; GNU C11 was explicitly used for these historical kernel builds.
Both used actual full preemption, MGLRU=0x7, the same power settings and a temporary
matched RT budget. The full configs and settings document the measurement, not a
request to overwrite a production system's configuration.

The compact data excludes warmup/diagnostic timings. It retains all formal
programs, including distributions and unfavorable whole-worker drift.
`verify_results.py` rechecks raw formal flags and statistics; it does not recreate
the earlier source/build/observer/recovery audit or certify a new run.
