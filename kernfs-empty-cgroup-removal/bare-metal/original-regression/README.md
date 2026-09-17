# Original regression and early follow-ups

This bundle retains 603 samples from the original four experiments.
The later [INODE_INITED](../inode-inited/README.md) and
[inode-request marker](../inode-requested/README.md) experiments are separate
sibling bundles. See the [experiment index](../README.md).

All experiments used one caller pinned to CPU0 on an Intel Core i7-12700KF
with 32 GiB RAM, SCHED_OTHER, actual full preemption, the performance governor
and Turbo disabled. Each sample contains 16 warmups followed by 128 measured
create/remove pairs. Each arm has nine samples per boot. The clock is
CLOCK_MONOTONIC_RAW. Probes and builds were absent during clean timing.

## Exact changes: 432 samples, 12 distinct boots

Each of `exact-r1` and `exact-r2` used fresh boots in this order:

```text
base-A → guard-A → notify-A → notify-B → guard-B → base-B
```

The exact base is `f917dc56060a10f401dd8ca46a1c5df237b35d84`; guard is its
child `507d8ce13f5b91d5b4dca7bd4b4e4249e8021cca`, and notify is the next
child `eea5d2bb34ba11dccd9c53f392dc50cf060150a9`. They are consecutive
source states on a 7.0-rc3 development baseline, not three released versions.
GCC 15.2 and kernel configurations except LOCALVERSION were matched.

Four arms were kept separate: original cgroup workload with callbacks off/on,
and the derived leaf control with sched_ext on/off. All four removal arms
passed both runs' CV ≤3%, absolute endpoint drift ≤2%, all endpoint increases
≥5%, and drop-first checks for guard/base, notify/guard and notify/base.

For `exact-r2/control-disabled`, guard/base was +6.047% to +6.947%,
notify/guard +37.332% to +38.081%, and notify/base +46.349% to +46.955%.
The first run's cumulative disabled-control increase was +46.205% to +46.826%.
Ranges describe the four A/B endpoint comparisons, not confidence intervals.

Creation did not meet the 5% regression criterion. The first run's original
callback-off creation comparison also had excessive notify endpoint drift.
These observations remain in the tables; the deletion claim does not imply
that every measured quantity was stable.

## Root reuse: 144 samples, four separate boots

`root-reuse-r1` used notify-A → prototype-A → prototype-B → notify-B.
Across four removal arms the prototype reduced latency by about 4–5%, or
0.38–0.49 µs. CV and drift passed, but no arm reached 5% improvement at every
endpoint and after dropping the first sample. This is one mirrored run, not
two independent repetitions. See [prototype limits](../../attribution/README.md).

## Pinned mainline: 27 samples, three separate boots

`mainline-r1` used v7.2-A → `2f0c1cf72f4682178506f513bbf015e591b1aa4a`
(7.3-rc2) → v7.2-B, with the same sched_ext-disabled control binary.

| Point | rmdir, µs | rmdir CV | mkdir, µs | mkdir CV |
| --- | ---: | ---: | ---: | ---: |
| v7.2-A | 9.919 | 0.750% | 18.608 | 7.044% |
| pinned 7.3-rc2 | 9.719 | 0.600% | 18.788 | 7.507% |
| v7.2-B | 9.953 | 0.505% | 18.767 | 7.227% |

Deletion improved 2.019% / 2.355%; old-end drift was 0.345%. Drop-first
improvements were 2.039% / 2.459%. Creation CV exceeded 3%, so its reference
medians do not establish an effect. Required features and runtime state were
matched, but the newer kernel has changed Kconfig options: the configurations
are not byte-identical. This is not attribution to one later commit.

## Compact data and verification

- `measured-rounds.tsv`: all 603 ordered invocation means, with creation and
  removal paired in each row. No invocation or long-tail sample was removed.
  Per-operation logs are not included; these are not all individual timings.
- `point-summary.tsv`: median of nine means, population CV and median after
  dropping the first invocation, separately for every arm/boot/metric.
- `comparison-summary.tsv`: endpoint effect ranges, drift, stability and
  the unchanged 5% directional criterion. Positive means slower.
- `identity.json`: exact source/build hashes where recorded, retained v7.2
  image identity, workload identity and 19 distinct boot records.
- `mechanism.tsv`: 48 untimed exact-source deletion windows; these are
  invocation counts in the operation chain, not a cost decomposition.
- `diagnostics.json`: separate root-reuse, pinned-mainline and inode-return
  diagnostic counts; none are timing measurements.
- `provenance.json`: hashes of the local source records and exported files.
  Source-record names identify provenance, not public raw-log downloads.

From this directory, `python3 -B verify.py` checks data/source hashes, all
603 samples, 134 point statistics, 58 comparisons and boot identities. It
does not contact a host, run a benchmark, or reproduce the private raw-trace
audit. The four full timing replays are also checked during local export.

The exact trace sees 26 nodes per deletion. Guard increases attribute
write-lock entries from 26 to 52; notify adds 26 same-chain inode lookups
and 26 wrapper read-lock entries. IRQ activity is excluded. A separate
return-value probe found 1 hit/25 NULL results in each original window;
holding two additional files open gave 3 hits/23 NULL results. Across 20
windows there were 520 lookups. A NULL result is not proof that a lookup
can safely be omitted. Later-version checks see the same deletion shape.
