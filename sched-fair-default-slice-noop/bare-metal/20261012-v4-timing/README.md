# Fourth prototype independent timing on October 12

After the separate [correctness batch](../20261012-v4-correctness/README.md), fresh boots ran original v7.2 A → v4 → original v7.2 B. These are new measurements, not reused v3 values. Mean ns/call:

| Case | Original A | Prototype | Original B | Change vs A/B midpoint |
| --- | ---: | ---: | ---: | ---: |
| Unchanged default slice | 615.984 | 162.390 | 617.031 | -73.660% |
| Unchanged custom 10 ms slice | 162.685 | 163.222 | 162.452 | +0.401% |

The default case saves 454.117 ns/call (73.660%); the custom fast path adds 0.653 ns/call (0.401%). Original drift is 0.170% / −0.143%; maximum within-run CV is 0.167% / 0.292%. Both satisfy the recorded 2% drift and 3% CV limits. [summary.tsv](summary.tsv) includes unpooled point means, absolute changes and drop-first sensitivity.

Timing uses one ordinary SCHED_OTHER/nice-0 task pinned to CPU 2, three warm-up rounds and 15 measured rounds of 500,000 calls per case per boot. Runtime preemption is full; governor/EPP are performance and Turbo is off. Probes run separately, never during timing. Latency includes the syscall loop and return-value checking, not just a bare kernel function.

There are 90 measured samples, not 90 independent boots. Original `7.2.0-bm-7.2` and prototype `7.2.0-fs-no4` use GCC 15.2 and matching Kconfig apart from LOCALVERSION. Both boot with `preempt=full crashkernel=0`, and actual full mode is checked.

Separate 32-call probes see fair updates/reweights on all original default calls and none on v4 default calls. Both custom controls skip the update. This supports avoiding redundant updates; it is not a PI performance claim, an application benefit, or full attribution of the earlier release slowdown. Real parameter-change timing has not been repeated for v4.
