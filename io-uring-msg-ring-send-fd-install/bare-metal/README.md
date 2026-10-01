# Bare metal evidence and methods

The current evidence is grouped by the question being tested, not by each run
or debugging attempt. Start with the relevant result page:

| Directory | What it answers |
| --- | --- |
| [original-regression](original-regression/README.md) | Exact original slowdown, registration plus first fill, and original-commit TCP impact |
| [dedicated-slab32](dedicated-slab32/README.md) | Whether Uzair's old patches 1+2 improve management and TCP costs |
| [shared-slab128](shared-slab128/README.md) | Design and results of an independent fixed-limit shared-kmalloc prototype |
| [uzair-v3-prefill](uzair-v3-prefill/README.md) | Historical large-cache/prefill benefit, small-demand control and memory cost |
| [history](history/README.md) | Earlier allocator and caching diagnostics that remain useful context |

## Follow-up measurement contract

All new comparisons use one physical Intel Core i7-12700KF machine with 32 GiB
RAM. The single test process runs on P-core logical CPU 2; CPUs 0–19 remain
online and sibling CPU 3 is not offlined. The matched points use GCC 15.2.0,
actual `preempt=full`, performance governor/EPP, and Turbo disabled. Detailed
build identities and the command line with storage identifiers removed are in
each `provenance.json`. Release names differ; canonical Kconfig identities
match within each comparison. The prototype is explicitly base plus patch,
not the unmodified base tree.

The harness used a 65,536 file-descriptor limit and 64 MiB process memlock
limit. These are process limits, not changes to the global network settings.

Each comparison first passed separate non-timing semantic and path checks,
then used three new boots: control A → variant → control B. Clean timing had
no probes. There were three warmups, then 15 groups of 32 management operations
or four TCP operations. Statistics use group means and sample standard
deviation. A result is stable only if **maximum point CV < 3% and absolute
control return drift < 2%**, both for all groups and after dropping the first
group. No clean timing was repeated to obtain a passing result.

The control is the midpoint of its two boot means. Positive percentages mean
the variant took longer. A 5% change is the predefined material-effect
threshold, not a confidence interval or a claim of statistical significance.
All unstable results remain in the data and receive no qualified effect claim.

## What is timed

- **Registration plus first fill:** reserve a sparse table and install files
  with SEND_FD. Excludes ring creation, verification, destruction and business
  I/O. A 50 ms pause after each new ring is outside timing.
- **Same-ring removal plus refill:** remove and replace the full used batch on
  a retained ring. Excludes initial registration/cold fill and final destruction.
- **TCP:** distinct loopback connections, SEND_FD, one or 16 request/echo
  exchanges of 64 bytes per connection, then removal and close. Four-connection
  cases create new rings per batch; 64/256-connection cases reuse rings and
  replace connections as a batch. One process handles both ends on CPU 2.
  This is neither the original liburing proxy nor external-network throughput.

The TCP total includes connection setup, source-table update, installation,
payload checks, I/O and close. In reuse mode, one-time ring setup/destruction
are recorded separately; the listener is also outside the per-batch interval.
`io_ns` includes source-reference removal and userspace validation, so it is
not a pure network-stack cost. `once_setup_ns` and `once_cleanup_ns` repeat in
rows for convenience and **must not be summed once per row**. A close return
or the 50 ms untimed gap does not prove all asynchronous reclamation or TCP
TIME_WAIT has ended.

Semantic checks cover paired CQEs, returned slot identities, queue state,
sentinel or TCP payload readback, EOF and cleanup. Function-entry counts are
direct-hit evidence, not complete branch coverage. The shared-128 node
inventory is also checked independently of timing.

## Compact data and independent recalculation

Each of the three current result directories has three data files:

- `timing.tsv`: all trial values, including warmups, combined into one file;
- `results.tsv`: all phase metrics, including unstable results and drop-first;
- `provenance.json`: source/build/runtime identities, numeric non-timing probe
  records, and data hashes. Private host paths and signing material are omitted.

From this target directory, run:

```sh
python3 -B reproducer/verify_followups.py
```

This reads only published files, recomputes 156 metrics from 26,586 trial rows,
checks stability decisions and 108 shared-128 inventory snapshots, and does not
run experiments or alter the host. [Build and run instructions](../reproducer/README.md)
are separate. The historical v3 data keep their existing self-contained format.
