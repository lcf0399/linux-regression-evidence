# Unchanged default-slice scheduler updates

On v7.2, the fourth prototype reduces repeated unchanged-default `sched_setparam()` calls from **616.51 to 162.39 ns/call**, saving **454.12 ns (73.66%)**. The existing custom-slice fast path costs 0.65 ns more (0.40%). These are syscall microbenchmark results, not an application speedup.

The revision also handles the two equal-runtime cases raised in review: switching from default to custom mode, and an implicit uclamp reset after RT-PI ends. The original kernel and third prototype both miss those cases in the tests.

[中文](README.zh-CN.md)

## Scenario and mechanism

An ordinary SCHED_OTHER, nice-0 task repeatedly calls `sched_setparam(0, {.sched_priority = 0})` on itself without changing its parameters. The main case uses the default slice; the control uses a custom 10 ms slice. The performance test is not a PI workload.

For default mode, the legacy wrapper supplies `attr.sched_runtime = 0`, while `p->se.slice` holds an actual duration. Comparing these values forces an update even when the task already uses the current default. The fourth prototype compares the modes before the duration, and retains updates for stale defaults, implicit clamp resets and tasks outside the actual fair scheduling class. Explicit uclamp updates retain their existing path.

## Results by experiment batch

| Batch | Question | Result |
| --- | --- | --- |
| [2026-09-02 release r1](bare-metal/20260902-release-r1/README.md) | v6.12.95 → v6.18.38 → v7.2 → v6.12.95 | Default no-change case +6.505%; separate custom control −3.819%. |
| [2026-09-02 release r2](bare-metal/20260902-release-r2/README.md) | Independent repeat of the release sequence | Default no-change case +7.082%; custom control −3.440%. |
| [2026-10-08 third prototype](bare-metal/20261008-v3-noop/README.md) | Same-v7.2 no-change optimization | 618.184 → 163.229 ns/call; later equal-runtime counterexamples apply to this version. |
| [2026-10-08 actual parameter changes](bare-metal/20261008-v3-parameter-changes/README.md) | Separate third-prototype `sched_setattr()` controls | Custom-duration and mode changes +0.096% / +0.716%; not fourth-prototype measurements. |
| [2026-10-12 Peter boundary checks](bare-metal/20261012-peter-boundaries/README.md) | Original v7.2 versus third prototype | Both reproduce two missed updates; explicit-update controls work. No timing. |
| [2026-10-12 fourth-prototype correctness](bare-metal/20261012-v4-correctness/README.md) | New prototype versus original v7.2 | Both missed updates handled by v4 in the tested combinations; other bounded controls pass. |
| [2026-10-12 fourth-prototype timing](bare-metal/20261012-v4-timing/README.md) | Fresh original → v4 → original sequence | 616.507 → 162.390 ns/call, −73.660%; custom control +0.401%. |

Each batch includes a short explanation and a TSV result table. Values are taken from the recorded experiment summaries; different sequences are not pooled and control costs are not subtracted. [Rejected early prototypes](attribution/rejected-prototypes.md) remain distinguished from the tested fourth revision.

## Platform and scope

- Intel Core i7-12700KF bare metal; the measured task is pinned to CPU 2.
- Prototype comparisons use v7.2 and GCC 15.2, with matching Kconfig symbols apart from LOCALVERSION.
- Timing uses one ordinary SCHED_OTHER/nice-0 task pinned to CPU 2, three warm-up rounds and 15 measured rounds of 500,000 calls per case per boot. Runtime preemption is full; governor/EPP are performance and Turbo is off. Probes run separately, never during timing. Latency includes the syscall loop and return-value checking, not just a bare kernel function.
- The September release builds and October prototype baselines are not a single matched cross-date experiment. Do not subtract their absolute costs or claim that the prototype explains the earlier release difference.
- The checks cover the recorded modes, RT/DL PI combinations, explicit uclamp, ordinary syscall controls and bounded default changes. They do not establish all-configuration safety or an application gain.

## Code and report state

- [Reproducer and commands](reproducer/README.md).
- [Tested fourth prototype](attribution/default-slice-peter-v4.patch); [historical third prototype](attribution/default-slice-noop-v3.patch).
- Initial RFC sent October 9; intact third-version patch attached October 10. Peter replied October 12. The fourth-version reply and tested patch were sent on October 13 at 17:58 UTC+08:00. Submission does not establish acceptance or merge; public archive visibility has not been rechecked for this reply.
- The RFC was assisted by LLMs. Measurements are from recorded bare-metal runs, not model estimates. Neither prototype is presented as an accepted upstream fix.
