# Third prototype no-change test on October 8

Original v7.2 A → v7.2 plus v3 → original v7.2 B. Mean ns/call:

| Case | Original A | Prototype | Original B | Change vs A/B midpoint |
| --- | ---: | ---: | ---: | ---: |
| Unchanged default slice | 619.402 | 163.229 | 616.967 | -73.595% |
| Unchanged custom 10 ms slice | 162.511 | 162.635 | 162.684 | +0.023% |

[summary.tsv](summary.tsv) gives the absolute saving, drift, CV and drop-first sensitivity. There are 90 measured samples: two cases, three boots, 15 samples per case. Both cases pass the recorded 2% original-drift and 3% within-run CV limits.

Timing uses one ordinary SCHED_OTHER/nice-0 task pinned to CPU 2, three warm-up rounds and 15 measured rounds of 500,000 calls per case per boot. Runtime preemption is full; governor/EPP are performance and Turbo is off. Probes run separately, never during timing. Latency includes the syscall loop and return-value checking, not just a bare kernel function.

Original kernel `7.2.0-bm-7.2`, prototype `7.2.0-fs-no3`; GCC 15.2 and Kconfig symbols match apart from LOCALVERSION. Separate 32-call probes see 32 fair updates/reweights on each original boot and zero for the v3 default case; custom controls see zero at all three points.

Preceding checks covered 15 syscall cases per kernel, RT/DL PI, mode/default changes and bounded concurrent defaults. The tested default-runtime RT-PI reset was preserved. The later [custom equal-runtime counterexamples](../20261012-peter-boundaries/README.md) were not covered by that initial check and reproduce in v3. These performance numbers therefore do not certify v3 as fully correct.
