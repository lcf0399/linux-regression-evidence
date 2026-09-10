# Uzair v3: first fill, registration, and same-ring reuse

Measured on 2026-09-10. The unchanged v3 series reduced the original
4,096-slot first fill by **9.791% against unpatched** and **8.819% against
0001+0002**. A separate 4,096-file same-ring remove/refill interval improved
by **18.820% / 18.584%**, respectively. Registration through first-fill
completion did not become cheaper.

This is patch validation on exact public **v6.18-rc4**, not a v7.2 test,
a complete ring-lifetime comparison, or general READ/WRITE performance.
The earlier exact-commit regression result is unchanged.

## Source and measurement identities

| Role | Content | Local test commit |
| --- | --- | --- |
| U | unmodified public v6.18-rc4 | `6146a0f1dfae5d37442a9ddcba012add260bceb0` |
| B | v3 0001+0002, source diffs matching tested v2 | `2359f858fa8db024958efc2eb7ac43a9002b44fe` |
| C | complete, unchanged v3 0001+0002+0003 | `3ad37b35866d99d62b992c53a1f45418f5fa76f2` |

[`identity.json`](identity.json) records complete trees, original patch
byte hashes, build/artifact hashes, workload identities, and all 12 timing
boot identities. The original collaborator patches and private email are
not redistributed here.

Both completed timing matrices used independent boots in this order:

```text
U-A -> B-A -> C-A -> C-B -> B-B -> U-B
```

The i7-12700KF workload process was pinned to logical CPU 2, with CPUs 0–19
online, microcode 0x3e, performance governor/EPP, and Turbo disabled. Kernels
used GCC 15.2, matched canonical Kconfig and build controls. Although the
common command line requested `preempt=none`, dynamic preemption was
explicitly set and verified as **actual `full`** before every measurement.
Clean timing ran with tracer `nop` and no enabled trace events.

- Original F0: unchanged binary, a source ring with one fixed file, and a
  new target ring with 4,096 sparse slots per round. Time 4,096 SEND_FD
  installations at QD 64, including SQE/CQE handling, but excluding
  registration, semantic readback, unregister, and destruction.
  Each boot has 3 warmups and 15 measured rounds.
- Original setup auxiliary: three registered/used shapes,
  `4096/4096`, `4096/64`, and `128/128`; 3 warmup operations followed by
  15 groups of 32 new rings. Registration, first fill, and the combined
  interval are reported separately.
- Lifecycle auxiliary: new workload identity; 3 warmup operations then
  15 groups of 32 operations for each of six cases. `serial` adds no gap;
  `paced` waits 50 ms after closing each target, outside timing.
  `reuse` keeps the same ring/table, removes installed files with
  FILES_UPDATE(-1), and refills using SEND_FD. Initial registration/cold
  fill, semantic readback, and final destruction are excluded.
- The original binary retained its 8 MiB memlock limit; both auxiliary
  workloads used a process-local 64 MiB limit. UID, CPU, and the compared
  binary stayed matched within each experiment.
- Auxiliary first-fill intervals exclude ring creation, readback,
  unregister/destruction, and any added gap. They are **not complete
  lifetime costs**. Reuse intervals also exclude initial setup.
- All installations were checked through paired source/target CQEs,
  unique returned slot identities, fixed-file sentinel readback, and
  drained queues. Tracing/counts were separate from clean timing.

## Original F0 result

| Role | ns/install midpoint | CV at the two boots | Signed boot drift |
| --- | ---: | --- | ---: |
| U | 119.423332 | 0.282% / 0.777% | +1.280% |
| B | 118.151131 | 0.930% / 1.920% | +0.158% |
| C | 107.731120 | 1.429% / 0.930% | -0.473% |

All primary checks passed. Drop-first remained -9.766% versus U and
-8.822% versus B. These are measurements of the actual v3 patch, not the
older diagnostic prefill kernel or a requirement to reach 104 ns/install.

## All auxiliary timing results

Lower is better. U/B/C means are averages of the two boot means; comparisons
use unrounded numbers. `NA` means an effect percentage is not qualified
because a stability check failed, not that no cost or benefit exists.

### Original setup auxiliary

| Scenario | Metric | U (us/batch) | B | C | C/U | Stability |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| 4096/4096 | registration | 1.509 | 1.499 | 50.909 | NA | gate-fail |
| 4096/4096 | first_fill | 478.183 | 478.077 | 434.270 | -9.183% | pass |
| 4096/4096 | register_plus_fill | 479.708 | 479.591 | 485.197 | +1.144% | pass |
| 4096/64 | registration | 1.987 | 1.961 | 63.189 | NA | gate-fail |
| 4096/64 | first_fill | 8.196 | 8.488 | 7.509 | NA | gate-fail |
| 4096/64 | register_plus_fill | 10.199 | 10.465 | 70.716 | NA | gate-fail |
| 128/128 | registration | 0.379 | 0.389 | 2.436 | NA | gate-fail |
| 128/128 | first_fill | 15.994 | 16.063 | 13.973 | NA | gate-fail |
| 128/128 | register_plus_fill | 16.389 | 16.468 | 16.425 | NA | gate-fail |

The full-table fill and combined interval passed. Registration alone and
all low-use/small-table metrics failed stability. These original results
remain separate from the changed-cadence follow-up.

### Lifecycle follow-up auxiliary

All values below are per complete batch, not per install. In reuse cases,
`interval` covers removal start through refill completion. In fresh-ring
cases, it covers registration start through first-fill completion.

| Scenario | Metric | U (us/batch) | B | C | C/U | Stability |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| serial-4096-64 | registration | 2.079 | 1.985 | 64.637 | NA | gate-fail |
| serial-4096-64 | fill | 8.259 | 8.850 | 7.408 | NA | gate-fail |
| serial-4096-64 | interval | 10.354 | 10.851 | 72.062 | NA | gate-fail |
| paced-4096-64 | registration | 5.955 | 5.933 | 71.218 | NA | gate-fail |
| paced-4096-64 | fill | 14.379 | 15.232 | 13.164 | -8.451% | pass |
| paced-4096-64 | interval | 20.361 | 21.193 | 84.407 | NA | gate-fail |
| paced-4096-4096 | registration | 6.032 | 5.875 | 71.881 | +1091.558% | pass |
| paced-4096-4096 | fill | 512.329 | 514.762 | 454.967 | -11.196% | pass |
| paced-4096-4096 | interval | 518.387 | 520.663 | 526.873 | +1.637% | pass |
| paced-128-128 | registration | 2.080 | 1.948 | 5.066 | NA | gate-fail |
| paced-128-128 | fill | 22.536 | 23.492 | 20.174 | NA | gate-fail |
| paced-128-128 | interval | 24.643 | 25.469 | 25.268 | NA | gate-fail |
| reuse-4096-4096 | removal | 233.062 | 235.617 | 145.041 | -37.767% | pass |
| reuse-4096-4096 | fill | 479.372 | 474.751 | 433.305 | -9.610% | pass |
| reuse-4096-4096 | interval | 712.451 | 710.384 | 578.365 | -18.820% | pass |
| reuse-4096-64 | removal | 2.472 | 2.463 | 2.485 | +0.516% | pass |
| reuse-4096-64 | fill | 6.971 | 6.934 | 6.961 | -0.141% | pass |
| reuse-4096-64 | interval | 9.460 | 9.413 | 9.463 | +0.031% | pass |

The large reuse interval had maximum group CV 0.105% and maximum boot drift
0.596%; drop-first agreed. Both removal and refill improved. Small reuse
was effectively unchanged: all roles already avoided new allocations.

For paced 4096/64, C's combined-interval boot means were 83.350 and
85.465 us, a +2.537% drift above the 2% limit. Its large mean cost increase
and retained-object counts remain an up-front-cost concern, but no precise
qualified slowdown percentage is assigned. For paced 128/128, U's
combined interval drifted -2.580%. Serial 4096/64 had maximum combined
group CV 27.500% and drift 9.187%. Failed cases were not rerun merely to
obtain passing numbers.

The changed cadence can alter cache and scheduling state. Do not substitute
paced absolute timings for the original F0 result, or infer a teardown
causal effect by subtracting serial from paced.

## Independent mechanism and resource checks

[`mechanism-summary.json`](mechanism-summary.json) contains the three-role
original-binary checks, five fresh-ring boundaries, lifecycle observations,
per-cycle reuse counts, cleanup counts, and 15 local failure-path checks.

### Original binary and cap

All roles made 73,985 logical `io_rsrc_node_alloc()` calls. For C, all
19 target fill cycles made zero cache-miss allocations. Its one remaining
miss belonged to the source ring's ordinary fixed-file registration.
Logical node handling was not eliminated.

The independent registered/used cases were 128/128, 4096/4096, 4096/64,
4096/0, and 8192/8192. All passed their semantic, cache-state, and cleanup
checks. C prefilled 4,096 nodes for the 8,192-slot case; installing the
remaining 4,096 files needed 128 full 32-object bulk calls.

### Repeated allocation avoided

Each of four independent non-timing reuse cycles observed:

| Refill shape | U | B | C |
| --- | ---: | ---: | ---: |
| 4,096 files | 3,968 single-object allocations | 124 bulk calls, 3,968 objects | no new objects |
| 64 files in 4,096 slots | no new objects | no new objects | no new objects |

Every large cycle still made 4,096 logical node-allocation calls. The larger
cache avoids repeated underlying allocation, not just the initial cost.
Final cached-node freeing was 128 objects for large U/B reuse and 4,096
for C; every tracked cache ended empty with a null entries pointer.

### Retained memory and failure boundary

For C's 4096/64 case, cached nodes were 4096 after registration, 4032
after fill, and 4096 after unregister. All cached nodes were freed on
final ring destruction. The cap uses registered capacity, not future
utilization. These are idle management objects, not extra open files.

Node payload is 24 bytes; allocator stride in this configuration is
32 bytes. A full 4,096-object cache occupies 128 KiB of allocator object
slots plus a 32 KiB pointer array. These counts are not process RSS or
an exact incremental physical-page measurement. Retention does not
guarantee hot CPU caches, immediate physical-page return, or completed RCU
callbacks at the exit-work boundary.

Fifteen tests of the actual cache C code with deterministic allocator
stubs passed ASan/UBSan/LeakSanitizer, covering small/capped sizes,
existing objects, failures, and cleanup. This is not kernel fault
injection or exhaustive concurrency validation. Ordinary non-sparse
registration skips the new prefill call by source inspection.

If array growth succeeds but the later bulk allocation fails, existing
nodes survive while the enlarged array/capacity remains. Thus failure
does not always leave the complete cache state unchanged. The tested
patch was not silently modified. On this base `init_clear=0`; do not
reuse the older `kzalloc` diagnostic's full-zeroing description for v3.

### Asynchronous teardown

For every role and each no-gap shape, rings 2–16 started while an older
ctx's exit work was unfinished. The paced observations had 0/16 such
starts. The longest observed close-to-exit-work-return window was
27.542 ms. These are non-timing observations, not CPU execution durations.

Actual node-cache-free windows overlapped the operation interval only
1/16 times in each no-gap 4096/4096 case and 0/16 in the other shapes.
Asynchronous exit already occurs in U. This does not prove it caused the
earlier high CV, or that every 50 ms gap in uninstrumented runs was a
synchronous cleanup guarantee.

## Data and reproduction

- [`timing-summary.tsv`](timing-summary.tsv): all 28 metric summaries;
  13 pass, 15 fail stability. Includes failed checks, CV, boot drift,
  and drop-first. Failed effect-percentage fields are deliberately `NA`.
- [`measured-groups.tsv`](measured-groups.tsv): all 900 measured rounds/
  groups used for the means, sample CV, and drop-first. F0 uses
  `first_fill_ns_per_install`; auxiliary columns are ns per operation,
  averaged over the explicitly listed 32 replicates. F0 `fill_ns`/
  `interval_ns` retain its full 4,096-install elapsed time.
- [`individual-tail-summary.tsv`](individual-tail-summary.tsv):
  all 108 per-point/per-metric individual-operation distributions from
  the follow-up, including maxima. These are not established production
  P99 improvements; group CV is not individual-operation CV.
- [`identity.json`](identity.json): source/build/binary/boot identities
  and hashes of original source tables and summaries.
- [`registration_cost_original.c`](registration_cost_original.c) and
  [`registration_cost_lifecycle.c`](registration_cost_lifecycle.c):
  the two auxiliary implementations. Only their relative include path
  was adapted for this bundle; both original and exported SHA-256 values
  are recorded. No experiment code or original F0 source was changed.

From this directory, compile the auxiliary sources:

```sh
gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror registration_cost_original.c -o /tmp/msg-v3-setup
gcc -O2 -g -std=gnu11 -Wall -Wextra -Werror registration_cost_lifecycle.c -o /tmp/msg-v3-lifecycle
```

Example clean auxiliary invocations on an appropriately prepared test host:

```sh
ulimit -n 65536
ulimit -l 65536
/tmp/msg-v3-setup --timing 4096 4096
/tmp/msg-v3-lifecycle --timing paced 4096 4096 50000 /tmp/v3-paced-unique.tsv
/tmp/msg-v3-lifecycle --timing reuse 4096 4096 0 /tmp/v3-reuse-unique.tsv
```

Use fresh output paths; the lifecycle workload refuses to overwrite a file.
These commands do not select kernels, reboot, set runtime controls, or
produce the independent mechanism traces. Reproduction requires the
exact tested patches/configuration and verified controls above. The
compiled examples are not claimed to reproduce the original binary hash.

Stability required sample CV below 3%, absolute same-role boot drift
below 2%, and drop-first checks. For auxiliary results these checks use
the 15 predefined group means. All original individual samples, including
tails, are retained in the experiment archive; no outlier was discarded.
Aborted preflight/probe/partial runs remain separate and are not mixed into
the two completed matrices. Bulky raw traces, boot images, private patch
attachments, and correspondence are excluded from this compact bundle.
