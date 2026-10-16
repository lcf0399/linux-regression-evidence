# Fixed-MM F01 batch, 2026-10-15 UTC

This directory contains all 90 formal samples and 12 warmups from one matched
baseline A -> prototype -> baseline B experiment; no runs were added during
curation. CPU 2 on Intel Core i7-12700KF, 4 KiB pages, dynamic preemption full;
CPU 2/3 performance governor and turbo disabled during timing. GCC 15.2, same
configuration apart from the local-version suffix. Generic kernel and original
settings were restored after the experiment.

| Condition | Baseline A | Prototype | Baseline B | Combined baseline |
| --- | ---: | ---: | ---: | ---: |
| 15 holders, extended table retained | 3.307 us | 2.337 us | 3.222 us | 3.264 us |
| 8 holders, low-count control | 2.165 us | 2.082 us | 2.120 us | 2.142 us |

Each point has two warmups then fifteen formal invocations per condition,
alternating conditions in a fixed order. One invocation averages three
16-page munmap calls. Those three stages are dependent, not independent
performance samples. Only syscall-return wall time including clock reads is
measured; setup, fork, coordination, validation and teardown are outside it.
There are no probes in these samples. Recorded fault and context-switch counts
were zero; that does not establish the absence of every interrupt.

- [samples.tsv](samples.tsv): 102 invocation means, including warmup flags and slot positions.
- [stages.tsv](stages.tsv): all 306 timer rows, including noise counters.
- [summary.tsv](summary.tsv): recomputable means, change, drift, population CVs.
- [builds.tsv](builds.tsv): exact source, compiler, kernel/config/binary identities.
- [baseline.config](baseline.config), [prototype.config](prototype.config).
- [boundary-observations.tsv](boundary-observations.tsv): separate, non-timing boundary observations.

High-count mean change is -28.39% (-0.927 us); return drift is -2.56%.
Point CVs are 14.04%, 4.93%, 16.55%. The same 15 formal slot positions recur
at all three points; benefits depend on those positions and sample ranges
overlap. Most saving is in stages one and two. Low-count change is -2.82%,
comparable to variability/drift, not a clear control-path improvement.

Run `make -C ../reproducer check` to compile/self-test the program and recompute
the tables offline. This does not execute the memory workload. These compact
rows do not replace the private raw state/probe and restoration logs.
