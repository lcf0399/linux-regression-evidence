# Exact source and standalone reproduction

`remaining.c` is byte-identical to the source used in the reported runs:
SHA-256 `6c6182a0eb51ed47ac50ca8cf36cdc1ed406e78e3180ece66c5f7963971f7403`.
It retains unused earlier modes so that the source is not rewritten after
measurement. The standalone wrapper allows only the four F02 modes.

```sh
make check
sudo python3 run_once.py serial-thp --out /tmp/f02-serial-thp-once
```

`make check` is **offline**: compile, arithmetic self-test, parser tests and
reconstruction of the supplied data. It does not exercise swap or THP.
The second command runs one four-cycle invocation. Use distinct output
directories for later runs; it will not overwrite results or retry failures.

Requirements matching this test:

- An expendable bare-metal test machine, root, 4 KiB base pages, cgroup v2
  with memory and pids controllers already enabled, and at least 2 GiB available RAM.
- CPUs 2 and 4 for the workers and CPU 16 for the coordinator. The exact C
  source fixes these numbers. Different topology/affinity requires a new
  workload identity, not relabeling the data in this bundle.
- A single active file swap area at `/swap.img`, at least 64 MiB free swap,
  zswap already disabled. Do not disable production swap to force a match.
- THP and `MADV_COLLAPSE`, pagemap/kpageflags access, swap-cache readiness,
  and actual base-page fallback after PAGEOUT. Unmet state checks are
  failures, not samples to keep retrying until a favorable result appears.

The wrapper creates and removes only a private group with 128 MiB memory,
64 MiB swap and 16-task caps. It never changes boot settings, governors,
turbo, THP settings, global pressure or swap activation. Deferred swap
accounting at group removal is reported, not hidden as already paid cost.
The original measured matrix recorded zero final deferred swap charge.

For a matched comparison, boot the exact base and the same base plus
`../prototype/reclaim-order.patch`, with matching configuration/compiler,
and use a clean baseline → prototype → baseline sequence. The published
matrix used full preemption, performance on CPUs 2–5, turbo off, two warmups
and nine formal invocations per condition per boot. Rotate the four cases:
`serial-base`, `parallel-base`, `serial-thp`, `parallel-thp`. Record and
restore any machine settings you intentionally change; check that no
diagnostic probes are active before timing. The wrapper does not automate
or certify those full-machine steps.

The wrapper is a **new packaging convenience**, not the controller used
for the reported measurements. Its parser is tested against all 132 saved
outputs; the historical measurements retain the original controller,
binary and inputs. A local rebuild with a different compiler is a new
binary, not the measured GCC 15.2 binary.

Primary output is summed worker PAGEOUT + cached-refault CPU microseconds
per 8 MiB. Cached refault reads one byte per page after confirming that the
data is still in swap cache; it is not a cold storage read. See
[`../bare-metal/README.md`](../bare-metal/README.md) for timing exclusions.
