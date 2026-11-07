# Additional measured leads for review

Full exploratory archives remain local; references labeled “local record” are not public reproduction bundles.

This inventory supplements the eight standalone candidate bundles. It excludes
already reported work. The **22 review entries are not 22 independently
established bugs or 22 submission-ready reproducer packages**. Related operations
are grouped without assuming a common cause. Release comparisons, exact-commit
comparisons and same-version prototype evaluations are distinguished.

The Chinese [detailed review](README.zh-CN.md) describes workloads, measurements,
limitations and lower-priority observations. CPU time, operation wall time and
whole-program cost must not be interchanged. Older results lacking a v7.2 check
are not presumed either fixed or still present.

| ID | Workload and measured evidence | Main limitation |
| --- | --- | --- |
| R01 | Batched userfaultfd service: 6.12.95 to 7.2 operation CPU 24.205 to 28.605 ms/16 MiB (+18.18%); whole-program CPU +16.41%. Calibrated HRTICK on/off/on: 29.326/25.321/29.342 ms on 7.1.3, independently 28.840/24.580/28.494 ms on 7.2. The latter saves 4.088 ms/14.26% operation CPU, 12.73% whole-test CPU and 4.24% whole-test wall time; two operation controls change -0.14%/+0.72%. | 81 clean samples on 7.2, on-return drift -1.20%, main maximum CV 1.13%; the normal-fault whole-program wall control is noisier. Feature-toggle evidence is not attribution to default-enabling commit `9213aa4784cf`; its exact parent has not passed boot admission. HRTICK has a scheduling purpose. |
| R02 | Related CPU-history/TLB leads: HugeTLB's same-7.2 prototype reduces first shared-mapping permission cost from 69.259 to 7.463 us; another PMD condition from 115.569 to 51.437 us. For 4 MiB mremap, release means are 6.195 to 67.752 us; four-version diagnostics observe one extra CPU16 callback after CPU16-to-CPU2 history on 6.14.11/7.2, absent with CPU2-only history and on the two older versions. | mremap's new-endpoint drift is 16.70%, so the mean ratio is not a stable magnitude. Shared CPU-history/remote-TLB factors do not establish the same complete cause or mremap nested-gather behavior. No exact introducing-commit evidence or whole-application benefit; retain first-versus-repeated operation and relevance questions. Distinct from the already-fixed per-table PMD synchronization problem. |
| R03 | Related release paths: DONTNEED +537–574 us/256 MiB, guard installation +111 us/64 MiB, hot VMA unmap +215–229 us/128 MiB; base-page release also slower. Separate native MMTests time-unmap confirms +8.558%/41.68 ms for a roughly 25 GiB unmap and +1.656% full-recipe time on 7.3 versus 6.17.13. | Workloads and baselines differ. Time-unmap has evidence for several incremental execution costs, not a complete allocation of its total loss or proof that all older release leads have the same cause. |
| R04 | Loopback TCP: 6.12.95 to 7.2 adds 2.792/2.512 us per 32 KiB send/receive pair (+10.65%/+9.43%). | Bytes, principal calls and accounting counts match; branch-miss changes are diagnostic clues, not a root cause. Not a demonstrated memcontrol bug or equal wall-time regression. |
| R05 | SLUB cache destruction with unrelated RCU backlog: 10.034 to 12.148 ms wall time (+21.08%) on 6.12.95 to 7.2. | Synthetic backlog, destruction window only; no-backlog controls improve. Historical RT runtime budgets differ (no RT workers). Attribution is parked, application frequency unknown. |
| R06 | Driver mmap inserting 4096 base pages: 177.348 to 229.135 us on 6.12.95 to 7.2; mmap plus munmap windows +21.64%, batched control flat. | One mmap, not 4096 syscalls. Removing a suspected extra PTE construction did not improve time. Attribution parked; not a whole-program measurement. |
| R07 | loadavg at `826eec5b5efd` to merge `51512e22efe8`, fixed load base: 765.615/867.172/774.965 ns per complete read. Separate timerfd Clang pair: 471.269/520.973/468.786 ns. | Return-prediction clues and a shared boundary do not prove a common mechanism or identify a BPF subcommit. Timerfd was not part of the fixed-base loadavg test. |
| R08 | Core-scheduling group CREATE, exact `6b1c66c9cca9`: approximately +50–62 ns/+5.4–6.7% for an eight-thread sleeping group. | Management operation, not per-schedule work; state/RCU protection is necessary. Whole-commit cost, not isolated RCU overhead. |
| R09 | Privileged autogroup nice alternation: two 6.12.95-to-7.2 rounds about +9.39%/+9.30%. | Bypasses normal rate limiting; real frequency unestablished. Tested commits explain neither a full 5% effect nor the entire release difference. |
| R10 | sched_ext PARTIAL global batch handoff: exact `d206fbad9328`, two rounds exceed 5% against both parents. | Global condition only; FIFO/vtime do not consistently qualify. The change has an acknowledged database-versus-handoff tradeoff. Not the full release delta. |
| R11 | Complete `/proc/self/stat` reads: 6.12.95 to 7.2 +5.57%/+5.66% for one/eight-thread forms. | Direct cputime interfaces do not show the same result; aggregation/formatting path, no exact cause or application impact established. |
| R12 | page-idle bitmap, 64 MiB anonymous pages: 6.12.77 to 7.1 about 5109 to 6144 ns/page (+20.3%). | Diagnostic removal of anonymous locking is not a safe fix or exact commit test. Privileged diagnostic interface; **no v7.2 validation**. |
| R13 | Btrfs create/delayed-inode conditions on a bare-metal RAM block device: 7.0.14 to 7.1.3 +13.86%/+11.65%. | First exact candidate is negative; RAM backend is not a disk application. **No v7.2 validation**. |
| R14 | XFS post-reflink 4 KiB buffered overwrite plus fdatasync: 6.12.95 to 7.1.3 +5.84%, NVMe, unshared control. | `c91d38` exact pair +4.18% does not explain the entire gap. Parked; **no v7.2 validation**. Distinct from reported Btrfs clone/dedupe. |
| R15 | ramfs create/unlink and mkdir/rmdir: 6.12.77 to 7.1 about +9.4%. | An earlier ~20% exact attribution was invalidated by matched suffix/layout checks (~0.35%/0%). **No v7.2 validation**. |
| R16 | VFAT/exFAT direct I/O on loop: 6.12.77 to 7.1 write conditions around +12–14%. | Backend direct-I/O mode removes one delta but not another; not attributed to the common direct-I/O core. **No v7.2 validation**. |
| R17 | io_uring GETSOCKOPT(SO_TYPE): 6.12.95 to 7.1.3 +12.56%, direct syscall +1.86%. | Two exact candidates insufficient; AppArmor-off diagnostic still slower. Fixed-query frequency unknown; **no v7.2 validation**. |
| R18 | sched_ext sleeping-target affinity-mask change: v7.2 repeated selected direction +13.89%/+14.70% versus old endpoints. | Not executing-task migration throughput; no BPF callback in this condition. Exact `650952` test inconclusive, no new strong history candidate. |
| R19 | UFFDIO_ZEROPAGE: 6.12.95 to 7.2 62.182 to 71.525 us/16 MiB, +9.343 us/+15.02%. | Small local cost; whole-program mean +1.96%. Low priority, not established application regression. |
| R20 | tmpfs large-entry restore on owned zram: sparse 32 KiB reads cost 144.379 to 2960.138 us; an 8 KiB boundary punch costs 57.311 to 723.049 us. Post-operation residency changes from 19 to 4096 and from 10 to 1022 base pages respectively. Dense 16 MiB restore instead averages 29.24% less CPU. | Strong directional tradeoff, not stable ratios: sparse/boundary maximum CV 15.59%/20.55%; return drift also exceeds the gate. Two boots per endpoint, no exact-commit attribution or application claim. Full-test validation reads the remainder and can benefit. A separate base-page dense-restore lead is +0.683 ms/+8.05% versus single-boot 6.19.14, not versus 6.12.95; it does not share the established large-granularity explanation. |
| R21 | tmpfs lifecycle of 128 empty files: no explicit user xattr 305.649 to 328.227 us (+7.39%); one 256-byte xattr 387.886 to 456.708 us (+17.74%). Maximum CV 1.69%/2.38%, both endpoint returns below 0.45%. Serial 512 replacements add 41.16/43.26 us on one/four mounts. | Lifecycle precision and return checks pass, but replacement maximum CV 4.08%/4.31% exceeds the gate; parallel conditions are noisier. Not proven shared-cache contention or one common cause. Full-test CPU includes already-reported listxattr validation; deferred RCU completion and application impact are unmeasured. |
| R22 | Base-page proactive swapout, two workers totaling 4 MiB: old endpoint boots average 1.552/1.561 ms operation CPU, v7.2 boots 2.877/2.200 ms. Serial control has the same direction (old 1.352/1.250, new 2.100/2.016 ms). | Long tails, maximum CV 69.53% and new-endpoint return drift -23.55% prevent a stable regression percentage. No introducing commit or complete work attribution established. |

For the separate time-unmap condition, matching main-thread counters show
9.036% more kernel instructions with essentially unchanged frequency. Node
accounting, LRU/memcg and allocator categories account for about 79.8% of
the net additional location samples in one precise-instruction profile pair.
This is a location distribution, not an additive patch-time decomposition.
Matched historical series and the bounded prototypes remain separate in the
original attribution record (local record).
It supplements R03 without adding an independent issue count or reopening
the older generated scenarios. The final PTE-prototype check completed all
180 counter windows on November 5: instructions fell 1.348%, or 23.33 per
base page, while diagnostic window time fell only 0.295%. The earlier clean
0.513% saving remains separate. This bounded round is closed as a multi-source
cost finding, without a complete allocation of the historical loss or a
separate upstream report.

R20 combines Sonnet W02/Gemini W02 restore proposals with Gemini W03 deletion
work; R21 combines DeepSeek W03 lifecycle and Astra W02 replacement proposals.
Two separate matched parent/child/parent tests now support scoped R21 attribution
(108 clean samples each). Switching shmem to per-inode rhashtables in
`52b364fed6e1` increases the one-xattr lifecycle operation by 56.513 us/13.84%;
the no-xattr control changes by -0.49%. Sharing the cache in `1e7cd8a53b72`
reduces that operation by 44.973 us/9.61% and full-test CPU by 7.43%; its
no-xattr control changes by +0.46%. Operation precision and return checks pass.
The pairs have different baselines: neither their percentages nor absolute
deltas can simply be subtracted to explain the full v7.2 release difference.
A third exact candidate, pathname-cache change `8c888b31903c`, does not support
the main attribution: no-xattr operation +1.12%, one-xattr -0.58%, with precision
and return checks passing. Subsequent diagnostics associate the one-xattr
creation work with hash-table maintenance notifications, irq_work and worker
wakeups, not with an established explanation of all extra cost.

A same-v7.2 early-notification-coalescing prototype retains IRQ handoff,
concurrency barriers and destruction synchronization. Its bounded checks and
108 clean samples are complete: one-xattr operation means are
453.188/424.236/453.506 us (baseline/prototype/baseline), or -29.111 us/-6.42%.
Return drift is 0.07%, but maximum CV 3.14% exceeds the predeclared 3% gate;
this is an improvement lead, not a stable exact benefit. Close/delete saves
12.270 us/8.15% with precision and return checks passing; creation and full-test
CPU are noisier. Three operation controls change by less than 2%.
Earlier instrumented diagnostics reduced duplicate notifications, but worker
executions increased from 16 to 21. Total background CPU and broader callers
remain unevaluated; no-xattr creation and the full release delta remain open.
This bounded attribution/prototype round is closed for review, not declared fixed.

Codex-derived fixed-work controls are separate from those original ideas. The
scope is tmpfs with real large swapped entries and synchronous zram for R20,
not a general NVMe or default-tmpfs claim. Required correctness protections,
dense-restore benefits and the xattr design's memory-cost rationale are retained.
Full shmem results and dispositions (local record)
contain all 40 conditions, 11 versions, 13 boots and 4,653 clean samples.
The R21 attribution record (local record)
retains the separate pairs and prototype limitations. R22's
swap_state matrix (local record)
retains the raw tails and return differences.
The COLD state-dependent tradeoff already has a
[standalone candidate](../mm-madv-cold-deactivation-tradeoff/README.md), so it is
not duplicated here. R01/R02/R03 and R20/R21/R22 remain review entries; indexing
them does not automatically reopen experiments.

## R07: related external Redis evidence (2026-11-01)

The native MMTests Redis benchmark, not the four-model-generated workloads,
identified `a3af84b0fa00` ("x86/bugs: Enable IBPB flush on BPF JIT allocation")
inside the same `51512e22efe8` merge. ZPOPMIN throughput is about 12.87% lower
on v7.3 than v7.1.3; the native child has a 13.59% loss in the separate
baseline-anchored comparison. This is not an equally sized whole-Redis loss.

On the unchanged fast parent, disposable tasks opted into stronger prediction
protection and exited before Redis ran. Two separate boots show 11.06% and
11.69% lower ZPOPMIN throughput; a fresh-boot sham return recovers, with 1.33%
baseline drift. The complete test takes only 0.35%/0.17% longer. All 27 complete
calls and 135 CSV files were independently audited; one native-client failure
is retained and excluded. The six-call same-parent diagnosis also reproduces
the RET-miss pattern: precise, newest-edge server RET samples rise from 0/1 to
624/626. The bounded mechanism investigation is closed as a persistent
post-flush return-prediction cost, not ongoing JIT work inside Redis requests.
The i7-12700KF uses microcode 0x3e; no mitigation disabling, MSR access or
microcode changes were used. An RSB-fill diagnostic retaining IBPB did not
restore performance. The same commit already has an upstream stress-ng.seccomp
report; this is supplementary evidence, not its first known performance cost.

See the bounded investigation (local record)
and the [supplementary report bundle](../../arch/x86/redis-ibpb-post-flush-a3af84b0/README.md)
prepared on November 2 and sent through local Gmail SMTP at 00:24 on
November 3 (China time). The sent body and reply headers were verified;
public archival has not been checked, and the evidence repository has not been synchronized.
The bundle packages this R07 evidence without counting another independent bug.
It does not establish the same precise cause for the older loadavg and timerfd
workloads.

## Related observations that are not promoted

- Fair queued affinity has same-kernel cross-boot drift and an incomplete PMU
  diagnostic. SCX fork follow-up and its CFS control are below 5%; ready-time
  improves. Neither establishes a stable whole-lifecycle regression.
- Memcontrol object handoff, cputime handoff and stop-task fair controls involve
  shared wake/sleep/scheduling paths, sometimes with high variation. They may
  overlap existing fair-handoff work, but a shared cause has not been proved.
- Huge-memory MOVE startup, debugfs splitting, all-failed reclaim, zero-page COW
  and tmpfs tail release retain their bounded dispositions. Local instruction
  or nanosecond/microsecond changes are not automatically application losses.
- HugeTLB demotion is noisy; pool/CMA/subpool release has small management costs
  while main operations or whole programs improve. These are not R02.
- Concurrent DONTNEED changes writer behavior; repeated DONTDUMP adds about
  10–18 ns/call; FREE and sparse PAGEOUT depend on selected intermediate
  baselines. One-page PAGEOUT is noisy. They are not discarded, but do not have
  the same evidence maturity as the main release-path entry.
- Pagetypeinfo has mismatched internal free-page state; UFFD COPY/UNMAP have
  unstable forms or controls below 5%. Expanded mseal does not retain its small
  signal; schedstats does more output work; the old large times() signal was not
  reliable. PSI/topology/cpu.max/cpupri fail stability or the target-version gate.
- io-wq loses its original attribution with matched configuration; other
  ext4 move-extent/fhandle/XFS-log records have direction, admission or stability
  limitations. Generated prefetch code is not measurement evidence;
  mremap's measured CPU-history lead is under R02 and measured shmem leads
  are under R20/R21. Shmem's untriggered cgroup/tail case is not a negative.
- Same-size mremap requests have a separate, low-priority observation: compared
  with the single 6.16.12 boot, 7.2's CPU for 4096 requests increases by 7.39%
  for one thread and 7.17% for two processes, about 7.66/7.46 ns per call.
  It is not slower than 6.12.95. The 6.16 baseline has no return boot and a
  realistic high-frequency use has not been established. This is not assigned
  to R02's TLB mechanism; see the original matrix addendum (local record).

mremap's 128-VMA whole-test increase is not promoted separately: approximately
98–99% of diagnostic whole-program CPU is spent reading/parsing maps and smaps
for validation. The prior +6.87% applies to that validation-heavy test, not an
established mremap-operation or application regression. Local source records:
release measurements (local record)
and bounded diagnostics (local record).

Already reported work, the existing standalone bundles, already-fixed PMD
synchronization, the existing swap-count early-exit optimization, source-only
schedutil recognition, withdrawn generated NOP work and early virtual-only
signals are not new entries here. The later native-MMTests shared-CQE result
has its [own candidate](../io-uring-mixed-cqe-nop-e26dca67/README.md), without
reopening the older generated case or duplicating it in this inventory.

## Priority and interpretation

After a file's bounded experiment round, retain valuable measured leads here
when attribution or independent-report readiness remains open, then advance the
main experiment queue. Add related operations to existing entries without
asserting an unproved common cause or counting another bug by filename. Keep
raw evidence in its original location; this index does not automatically reopen
experiments, publish evidence or send reports.

R01, R02 and R07 have useful feature/mechanism/adjacent-boundary evidence.
R03 now includes partial attribution for native time-unmap, without proving
a common cause for its older related operations; R04 remains unattributed. R08/R10 need their
protection/design tradeoffs stated alongside costs. R20 has a granularity
tradeoff; R21 has scoped commit costs and a later benefit, but not complete v7.2 attribution. R12–R17 require target-version
validation before renewed investigation. Parked and low-frequency cases do not
automatically restart because they are indexed here.

Continuation depends on workload relevance, frequency/scale, a clear question
and evidence quality, not on already having a patch. A concrete improvement
idea can still be evaluated within a bounded, function-preserving comparison.
