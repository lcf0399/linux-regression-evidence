# Release comparison r2 on September 2

Fresh boots: v6.12.95 old A → v6.18.38 intermediate → v7.2 new → v6.12.95 old B.
This is an independent repeat of r1, not a pooled estimate.
Mean ns/call, lower is better:

| Case | Old A | Intermediate | New | Old B | New vs old midpoint |
| --- | ---: | ---: | ---: | ---: | ---: |
| Unchanged default slice | 536.905 | 546.544 | 575.744 | 538.431 | +7.082% |
| Unchanged custom 10 ms slice | 169.178 | 165.190 | 162.415 | 167.225 | -3.440% |

[summary.tsv](summary.tsv) includes drift and maximum within-point CV. The default-slice case and its paired control passed the recorded stability and semantic checks. The custom control is reported independently; it is not subtracted from the main case.

Timing uses one ordinary SCHED_OTHER/nice-0 task pinned to CPU 2, three warm-up rounds and 15 measured rounds of 500,000 calls per case per boot. Runtime preemption is full; governor/EPP are performance and Turbo is off. Probes run separately, never during timing. Latency includes the syscall loop and return-value checking, not just a bare kernel function.

The actual runtime mode was full, although these release builds booted with `preempt=none` before switching mode. The v7.2 release was `7.2.0-iofxv72-good-8d3ae59288f1-xxxxxxxxxxxxxxxx`, not the October prototype baseline `7.2.0-bm-7.2`. These measurements motivated investigation; they do not identify an introducing commit or measure a prototype.

Only the two setparam cases are included here. Affinity cases from the same run belong to a different workload.
