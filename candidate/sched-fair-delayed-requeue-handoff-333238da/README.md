# Same-CPU synchronous handoff and delayed requeue accounting

Report candidate, 2026-09-21. Not submitted upstream; not an accepted or merged
fix. The revert used here is a diagnostic, **not a proposed optimization**.

On the tested v7.2 kernel, reverting `333238da9a19` ("sched: Update time before
requeueing delayed entities") reduced complete two-thread roundtrip time by
11.31% with no added computation and 9.58% with light computation. The effect
fell to 2.86% with more computation. With a third runnable thread, this matrix
did not show a stable material advantage. There is no measured application-level
impact or proof that the revert preserves scheduler fairness.

## Workload and attribution

Two nice-0 SCHED_OTHER threads, both pinned to CPU 2, alternate using private
futex WAIT/WAKE. Each thread performs 0, 256, or 4096 dependent integer operations
before each handoff. A full roundtrip contains two handoffs. Each setting is also
tested with a third CPU-bound thread on CPU 2. Checksums and protocol checks are
outside the timed interval; creation and initial setup are not timed.

The [original commit delta](attribution/333238da9a19.patch) adds
`update_curr(cfs_rq)` before delayed entities are requeued. Its purpose is to
compute lag from updated accounting rather than stale state. Omitting that call
changes scheduling decisions as well as work done: without the busy thread,
the reference makes about 2 WAIT calls/roundtrip, versus 1.14, 1.10 and 1.02 in
the revert at the three computation levels. The time difference is **not** the
isolated execution cost of one `update_curr()` call.

Independent traced runs observed 1994–2000 delayed requeues/1000 roundtrips on
the reference without the busy thread, but only 1–3 with it. Every observed
reference requeue included the nested accounting call; the revert had none.
These are separate instrumented observations, not exact frequencies from the
clean timing runs. Similar background throughput does not establish fairness
under the original frequent-requeue condition.

A separate [fixed state sample](attribution/placement-sample-summary.json), using
the earlier handoff-only program, recorded 32 requeues per kernel/nice condition.
At both nice 0 and +5, the reference repositioned the wakee in 31/32 samples and
selected the current task in 32/32. The revert repositioned none and selected
the wakee in 23/32 and 21/32. Sampling was fixed in advance; these proportions
are not uninstrumented population frequencies or a timing decomposition.

The [separate v7.3-rc4 handoff-only check](bare-metal/handoff-only-7.3-rc4-summary.json)
used unmodified `93f51579e7df` between two v7.2 boots. Roundtrip time changed by
+0.121% at nice 0 and −0.568% at nice +5, with maximum CV 0.377% and drift 0.130%.
It retained the approximately two-WAIT behavior. This check used the earlier
program, three warm-ups and nine measured rounds/case/boot, and is not pooled
with the six-case computation matrix below.

## Complete measurement matrix

Reference A → revert → reference B, each in an independent boot. i7-12700KF,
20 logical CPUs online; actual full preemption; performance governor/EPP,
Turbo disabled. Same GCC 15.2.0 and configuration symbols except LOCALVERSION.
Each case/boot has three invocations, each with one warm-up and three measured
rounds of 100,000 roundtrips: nine observations per case/boot, 162 in total.
All measured rows, including noisy cases, are retained.

Times are ns/roundtrip. Difference = revert / mean(reference A, reference B) − 1.
Maximum CV covers both the nine-round pool and each three-round invocation.
Frozen criteria: CV ≤2%, absolute reference drift ≤2%, material effect ≥5%.

| Busy thread | Work/handoff | Reference A | Revert | Reference B | Difference | Max CV | B/A drift |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| No | 0 | 2203.878 | 1953.822 | 2201.938 | −11.307% | 0.940% | −0.088% |
| No | 256 | 2758.329 | 2491.302 | 2751.968 | −9.577% | 0.233% | −0.231% |
| No | 4096 | 11350.495 | 11026.134 | 11351.376 | −2.861% | 0.099% | +0.008% |
| Yes | 0 | 3993.389 | 4200.725 | 4063.397 | +4.278% | 4.447% | +1.753% |
| Yes | 256 | 5142.982 | 5004.522 | 4985.720 | −1.181% | 3.500% | −3.058% |
| Yes | 4096 | 21525.327 | 21538.822 | 21489.921 | +0.145% | 0.498% | −0.164% |

The first two busy-thread cases are unstable; their percentages are descriptive
only. The last is stable but nearly unchanged. Background computation rates
differ by less than 0.11% in all three cases. Removing the first measured round
of each invocation leaves the no-busy-thread differences at −11.263%, −9.502%
and −2.879%. The machine was restored to its generic kernel and original power
settings after the run.

## Files and reproduction

- [Exact workload](reproducer/weighted_payload.c) and [Makefile](reproducer/Makefile).
- [All 162 measured rows](bare-metal/measurements.tsv), [six-case summaries](bare-metal/summary.json),
  [18 independent probe results](bare-metal/independent-probes.json).
- [Build identities](bare-metal/identity.json), [runtime settings](bare-metal/environment.json),
  [reference configuration](bare-metal/reference.config), [revert configuration](bare-metal/revert.config).

Compile with `make -C reproducer`. For one case on an already prepared kernel:

```sh
./reproducer/weighted_payload --cpu 2 --competitor 0 --work-iterations 256 \
  --ops 100000 --warmups 1 --rounds 3 --timeout-ms 10000
```

The command runs locally and does not configure the machine or reboot it.
The recorded matrix repeats each of the six cases three times per kernel.
Do not treat timings collected on an arbitrary development machine as a
reproduction of the matched bare-metal comparison.

Mail drafts remain ignored under `email/`; private source links remain under
`private-provenance/`. Full runner inputs, traces and raw logs stay in the
experiment workspace. Before reporting, refresh the relevant upstream audit
and recipient list. This bundle does not claim that no later fix exists.
