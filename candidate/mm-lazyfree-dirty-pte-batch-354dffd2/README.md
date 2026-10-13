# Lazyfree: failed PTE-batch work on redirtied large folios

October 13, 2026. Optimization-discussion candidate; not submitted upstream.
The change is in `mm/rmap.c`, found while studying a `huge_memory.c` workload.
The evidence supports avoidable work in a narrow path, not an established
application speedup. Retain the candidate; it is not an urgent regression report.

## Workload and mechanism

The synthetic workload allocates and touches 32 MiB of anonymous large folios,
uses `MADV_FREE`, rewrites the first byte in each 2 MiB range, and temporarily
changes then restores the second 4 KiB page's protection. This splits the PMD
mapping, not the physical folio. It then calls `MADV_PAGEOUT`, verifies retained
data and resident state, and unmaps the allocation. Reclaim fails in this state;
the measurement is not successful reclaim bandwidth.

Commit `354dffd29575cdf13154e8fb787322354aa9efc4` introduced batched lazyfree
large-folio unmapping for a useful success path. Here, clearing and later
restoring a batch after discovering dirty state adds work. The experimental
[v7.2 patch](prototype/dirty-hint-v7.2.patch) selects a batch of one when an
anonymous, non-`VM_DROPPABLE` folio or the current PTE already gives a dirty hint.
Post-clear checks, barriers, restoration and accounting remain. The early hint
is not a replacement for correctness checks or a proof against concurrent writes.
Independent observations changed four nested flush requests from 512-page to
one-page ranges; these are request ranges, not hardware instruction counts.

## Whole-lifecycle comparison

Unmodified v7.2 A -> prototype -> unmodified v7.2 B, matched GCC 15.2 and config
except LOCALVERSION; actual full preemption. One bare-metal host, CPU 2, all 20
logical CPUs online, test-CPU performance mode and turbo disabled. Each boot has
two warmups and nine measured invocations, each containing four 32 MiB cycles.
Four separate admission executions checked state and direct hits. Timing has no
probes. Data remains resident without swapout; generic and settings were restored.

Values are microseconds per 32 MiB cycle; change is relative to the two original
means' midpoint. These are observations from this matrix, not application results.

| Interval | Original A | Prototype | Original B | Change | Max CV | Return drift |
|---|---:|---:|---:|---:|---:|---:|
| PAGEOUT | 85.218 | 24.348 | 84.863 | -71.37% | 1.05% | 0.42% |
| Operation intervals, excluding checks | 2981.231 | 2942.003 | 3038.856 | -2.26% | 3.79% | 1.93% |
| Continuous allocation-to-unmap interval, including checks | 3941.071 | 3908.979 | 3997.972 | -1.53% | 2.78% | 1.44% |

PAGEOUT saves 60.69 us with complete sample separation. The lifecycle effects are
similar in size to variation and control drift, with overlapping samples. Passing
the original CV <= 5% / drift <= 3% flags does not establish precise small gains.
Allocation/first touch accounts for about 87% of the original operation subtotal.
Checks still run between operation intervals, even when their time is excluded.
Process startup, metadata-file opening and final output are outside these clocks.

## Other evidence, including limits

A separate earlier v7.2 matrix has 189 formal samples across seven conditions:
the original PTE case and a first-dirty-PTE control improved by 71.27% and 70.37%;
last-dirty-PTE and clean-PTE controls changed by +0.32% and +0.27%. Two PMD controls
and the original clean control were unstable and remain inconclusive. They are
not omitted from [the data](bare-metal/results.json). Those samples use different
programs and are not pooled with the lifecycle matrix.

The earlier exact parent/child/parent result was 36.975 -> 106.036 -> 33.410 us
per 32 MiB PAGEOUT. Parent: `2f4ab3ac10e1476abb6ed55f0b5f176cf635e776`;
child: `354dffd29575cdf13154e8fb787322354aa9efc4`. Samples separated, but maximum
CV was 22.17% and return drift 9.64%. This supports the slowdown direction and
attribution lead, not a precise stable regression percentage or full explanation
of the release gap.

Separate fixed-v7.3-rc7 application admission did not establish the target path.
Polars' 44 relevant large-folio calls used PMD mappings during extra validation;
the synthetic request pool's 160 relevant processing-phase calls also all used
PMDs. Neither compared the prototype's performance. These are non-hit results,
not measured zero benefits or proof that no matching application exists.

The patch has not exhaustively covered races, GUP or architectures; late dirty
PTEs do not benefit, and three controls remain unstable. v7.2 results do not cover
newer correctness fixes. No fresh upstream duplicate/fix audit was done here.
Before sending, check the then-current source, patch applicability, correctness
fixes and recipients. A suitable question is whether the failed-batch strategy is
worth adjusting while preserving the success path, not whether to revert it.

## Files and reproduction

- [Patch](prototype/dirty-hint-v7.2.patch): exact tested experimental patch, not a signed-off submission.
- [Program](reproducer/whole_probe.c), [unchanged helper source](reproducer/base_probe.c), [Makefile](reproducer/Makefile).
- [All 33 lifecycle records](bare-metal/whole-lifecycle.tsv), including six warmups.
  Nanosecond columns sum four cycles; divide by four and 1000 for the table above.
- [Summaries and all seven controls' formal samples](bare-metal/results.json).
- Raw-source locations and hashes remain in ignored `private-provenance/`.

```sh
make -C reproducer selftest
sudo env KS_HUGE_RUN=1 reproducer/build/whole_probe lazy_ptes 1 4 0
# One timed invocation, only after admission and matched setup:
sudo env KS_HUGE_RUN=1 reproducer/build/whole_probe lazy_ptes 4 16 1
```

Use a dedicated test machine. CPU 2, 4 KiB base pages, THP and access to owned PFN
metadata/kpageflags are required; failed assertions invalidate a comparison.
These commands do not configure power, THP, boots or independent probes. The
included helper retains other source routines to preserve the measured code;
the program's normal entry point accepts only `lazy_ptes`.
Only compilation and the non-running self-test were repeated during packaging.
LLM assistance was used for the investigation, prototype and reporting.
