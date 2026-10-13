# Fourth prototype correctness on October 12

The v4 mode-first prototype was tested against unmodified v7.2 before performance timing.

| Case | Original | Fourth prototype |
| --- | --- | --- |
| Equal-duration request must switch default → custom; default later changes | Slice follows default to 2.9 ms | Requested custom 2.8 ms is retained. |
| Same with explicit uclamp | Custom 2.8 ms retained | Same. |
| Custom 10 ms, RT-PI ends, equal-runtime legacy call | Implicit min stays 1024 | Implicit min resets to 0. |
| Same with explicit uclamp reset | Min resets to 0 | Same. |

These four cases ran plain and independently probed on both kernels: 16 executions. [observations.tsv](observations.tsv) retains all 16 outcomes and the bracketed function-entry counts. The original counterexamples remain classified as such.

Other checks comprise 15 syscall assertions per kernel plus plain/probed RT-PI, DL-PI, stale-default and 128-step concurrent-default controls. In total, this batch has 34 program invocations, including 30 syscall assertions and 32 boundary/control executions. V4 preserves the DL-PI EBUSY return; stale-default updates still occur. Both kernels pass the other tested controls.

GCC 15.2, matching v7.2 Kconfig apart from LOCALVERSION, runtime full preemption; no performance samples. Global slice and runtime settings and normal boot state were restored. Function-entry evidence and bounded concurrency checks do not establish every branch, configuration or race.

An initial local replay compared Python tuples with JSON lists and failed; normalizing that representation made the main replay agree with the independent replay. Kernel observations and frozen inputs were unchanged; no experiment was rerun to obtain a pass.
