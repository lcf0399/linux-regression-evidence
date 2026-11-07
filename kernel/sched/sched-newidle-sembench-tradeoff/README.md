# sembench and new-idle balancing

Major policy attribution and the bounded Bresenham v4 evaluation are complete.
Final offline review on November 5 independently re-read all 189 clean calls in
the matched revert, feature-toggle and v4 comparisons; the raw counts, exit status,
means, variability and return drift agree with the report. No new machine runs were needed.
The user confirmed sending the reviewed report on November 5, as feedback on the
posted Bresenham v4. The bundle has moved out of `candidate/` into the scheduler
section. See [upstream status](upstream-status/README.md); actual sent headers,
public archival and any subsequent replies have not been independently checked.
This workload was found while running **MMTests / native sembench**, not by the model-generated workload method.

On the same v7.3 image, disabling NI_RANDOM raises the 815-worker mean throughput by 23.48%,
with -0.31% baseline return drift; the 8-worker control changes by +0.34%.
The disabled group has 7.18% CV, but all nine runs exceed both baseline groups.
A separate matched v6.15.11 upstream revert recovers 14.03% / 17.18% at 350 / 815 workers.
These are different balancing-policy implementations, not one commit persisting unchanged through v7.3.

The native workers have single-CPU affinity, while the central waking thread may migrate.
Independent migration stacks and main-thread counters support a change in its migration and contention pattern.
This is a workload-sensitive balancing tradeoff; the feature toggle is a diagnostic control, not a production recommendation.

The v4 comparison uses fresh matched v7.3 baseline / patch / baseline boots, with the native 8 / 815-worker
conditions and nine measured calls per condition. Both NI_RANDOM and NI_RATE remain enabled.
The posted patch changes attempt timing and boundary behavior; it is not assumed to be an equal-count spacing-only change.
All 54 measurements completed on November 2 at 07:58:39 UTC, with independent raw-data
recalculation and generic-kernel recovery. At 815 workers, throughput was
1,112,251 / 1,136,952 / 1,117,457 burns/s: +1.98% against the baseline mean,
maximum CV 1.24%, return drift +0.47%. This small gain does not recover the main loss.
At 8 workers the mean improved by 7.68%, but the patch group's CV was 3.32%,
above the 3% precision threshold. No exact stable gain is claimed for that condition.

See the [Chinese overview](README.zh-CN.md) for the original evidence and active experiment links.
Correspondence and full exploratory archives remain local.
Further experiments are deferred unless a concrete upstream change or question warrants reopening this case.
