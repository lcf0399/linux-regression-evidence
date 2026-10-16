# Equal-runtime counterexamples on October 12

Unmodified v7.2 and the third prototype were tested without changing either kernel. Four cases per kernel ran once without probes and once with independent probes: 16 executions, no timing.

| Case | Original and v3 both do | Interpretation |
| --- | --- | --- |
| Request custom 2.8 ms while default is 2.8 ms; change default to 2.9 ms; call the legacy API | Slice becomes 2.9 ms | The initial request failed to switch to custom mode. |
| Same request with explicit uclamp | Slice stays 2.8 ms | Explicit-update control enters the required path. |
| Set custom 10 ms while RT-PI boosted; unboost; repeat an equal-runtime legacy call | Implicit uclamp_min stays 1024 | Required implicit reset is missed. |
| Same, with explicit uclamp reset | Implicit uclamp_min becomes 0 | Explicit-reset control works. |

[observations.tsv](observations.tsv) includes the two per-operation fair-update counts from each probe case; they agree between original and v3. Plain and probed runs agree on state.

Successful syscalls and a successfully recorded counterexample are **not** a correctness pass for the desired operation. These are inherited equal-value shortcuts, not two new bugs introduced by v3. The earlier RT-PI check used the default-runtime case; this batch adds custom equal-runtime state. Settings were restored after each case.
