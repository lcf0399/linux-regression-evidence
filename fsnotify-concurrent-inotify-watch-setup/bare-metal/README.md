# Bare-metal exact A/B summary

The exact experiment ran on an Intel Core i7-12700KF system with 32 GiB RAM.
The benchmark used eight physical P-cores (`CPU 0,2,4,6,8,10,12,14`), with the
scaling governor and energy-performance preference set to `performance`, Turbo
disabled, and runtime `preempt=none`.

Three independent boots formed this sandwich:

```text
6c790212c588 parent A -> 94bd01253c3d child -> 6c790212c588 parent B
```

Each point used two warm-up rounds and 25 measured rounds. Values in
[`exact-ab-summary.tsv`](exact-ab-summary.tsv) are medians. The P8 absolute
metric is aggregate worker add plus remove time per watch, not wall-clock
latency. The paired metric is the per-round distinct/shared ratio.

[`build-identity.tsv`](build-identity.tsv) records the exact commits and the
matched build/runtime identity. The config, compiler, Kbuild metadata,
module-signing key, preemption mode, and kernel-release string length were held
constant across the parent and child.

[`focused-mechanism-summary.tsv`](focused-mechanism-summary.tsv) is from a
separate five-boot same-commit diagnostic sequence. Its modified kernels are
useful only for attribution and must not be treated as candidate fixes.

## Scaling extension

[`scaling-extension-summary.tsv`](scaling-extension-summary.tsv) records a
second three-boot exact sandwich with P6, P8, and an auxiliary W16-SMT point.
P6 used CPUs `2,4,6,8,10,12`; P8 used `0,2,4,6,8,10,12,14`; W16-SMT used
logical CPUs `0-15`, including both SMT siblings of each P-core. Every topology
used 96 watches, two warm-up rounds, and 25 measured rounds, with the same
performance contract and exact parent/child build identity as above.
The frozen protocol SHA-256 was
`7f7800d2d52135b15f1159be8edeb3ab12dfb5456d0a708705111b527d140822`.

P6 and P8 passed both the absolute and paired `5%` signal, `5%` parent-drift,
and `15%` CV gates. The W16-SMT absolute case had the same direction, but its
child paired CV was `15.006954%`; the threshold was not relaxed after the run,
so that point is explicitly auxiliary. Existing-mask and path-lookup controls
did not move in the regression direction.

The earlier exact run had no material P1 or P4 distinct signal. Comparing the
independently sandwiched runs therefore supports a narrow onset between P4 and
P6 on this machine, not a universal concurrency threshold.
