# Reproducing the post-IBPB Redis result

These are the commands and boundaries used in the completed experiments,
not a request to disable a mitigation. Use a dedicated test machine and
an owned Redis instance in a private network namespace: `FLUSHALL` below
must never target an existing database. No experiment is started by
preparing this bundle.

## Fixed inputs

- MMTests: `638401e07e70d67fa5a1e6295e1315ae0af831b2`;
  `configs/config-memdb-redis-benchmark-small` and the Redis driver.
- Redis 6.2.6, unmodified, built with the default jemalloc allocator.
- Release comparison: v7.1.3 / v7.3 / v7.1.3, each on a fresh boot.
  Tested v7.3: `c03c1a14507dfaf8b856c2608f9ac216021fc222`.
- Exact introducing change: `a3af84b0fa00ead01fcd0e28b5d773ff25990a0d`;
  fast parent: `96cce16e26dd02a8678f1e87f88a4b5cdb63b995`.
- GCC 15.2.0; i7-12700KF, 32 GiB RAM, microcode 0x3e; CPU0–19 available,
  no client/server pinning. Runtime preemption is full, governor powersave,
  energy preference balance_performance, Turbo enabled, ASLR=2.
- Keep normal security settings. Record the CPU vulnerabilities files,
  command line, actual preemption mode, microcode and configuration at
  every boot. Exact configs and build identities are retained locally.

## One native test call

Temporarily select THP `never`, record and restore its previous value.
Use a private network namespace with loopback enabled and its own
`net.core.somaxconn=512`. Keep all test data in a fresh owned directory.
Run the server as the test user, using the stock Redis 6.2.6 persistence
configuration with only `pidfile` and `dir` redirected into that directory.
Do not disable persistence or add a CPU affinity restriction.

The completed runner used this sequence:

1. Start `redis-server <owned-config>` in the foreground, retaining its PID
   and logs. Wait the recipe's five seconds and verify that it is alive.
2. Run `redis-cli FLUSHALL` against this private instance and require `OK`.
3. Run the full command below five times, saving every CSV and stderr;
   require success and call `sync` after each repetition. Do not flush
   between these five repetitions.
4. Save `redis-cli INFO stats`; run a final `FLUSHALL`, then `SHUTDOWN`.
   Wait for the owned server to exit successfully. Restore temporary
   settings and retain failures rather than silently rerunning them.

```text
redis-benchmark --csv -c 1 -r 50000 -n 100000 -P 256
```

Do not use `-t zpopmin`. The full sequence first populates one sorted set
with ZADD, then drains it with ZPOPMIN without replenishment. At most
50,000 distinct members are available for nominally 100,000 requests;
at least half therefore return empty results. Pipeline completion can
round the executed request count, so preserve the native CSV rate rather
than deriving it from an assumed exact command total.

For each call, average the five native ZPOPMIN rates to obtain one sample;
do the same for GET and retain all other command results. Measure whole
test lifecycle wall time separately, including startup, synchronization
and cleanup. Five repetitions within a call are not five independent
boots. The release confirmation used a state check, one warmup call and
nine measured calls per fresh boot. There were no background RDB forks
in those 27 clean measured calls; check the server logs and `total_forks`
when reproducing, rather than assuming persistence can never run.

## Same-kernel protection intervention

Use the unchanged fast parent above. The supplied
[`ibpb-pulse.c`](ibpb-pulse.c) is byte-for-byte the helper used in the
completed experiment (SHA-256
`80667fc790fc8b085418365aa2c71b3861296947cf0be954297f5bf6ad53ce06`).
It is deliberately bounded to this machine's CPU0–19 and refuses root
or an unexpected initial speculation-control state. Do not weaken those
checks to force a pass on another configuration.

Compile before measuring, then run the selected arm as an unprivileged
user on a fresh boot:

```sh
cc -O2 -Wall -Wextra -o ibpb-pulse ibpb-pulse.c

# Sham boot: save the 20 JSON records and require every command to succeed.
for cpu in $(seq 0 19); do
    ./ibpb-pulse "$cpu" sham || exit 1
done

# On a DIFFERENT fresh boot of the SAME kernel, replace sham with protect.
# Do not run the two arms successively on the same boot.
```

The `protect` arm uses
`prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH,
PR_SPEC_FORCE_DISABLE, 0, 0)` in each disposable process. It strengthens
that process's protection; it does not enable speculation, write an MSR,
or change microcode. Each helper exits before the next starts. Start
Redis from the unchanged controller only after all 20 helpers have
successfully exited; do not launch it as a child of a protected helper.
The controller's `PR_GET_SPECULATION_CTRL` mask was 3 before and after;
each protected helper changed its own mask from 3 to 9.

Repeat the native test call using the same preparation and measurement
rules. Keep boot segments separate. The completed data contains nine
sham-first calls, four valid protection calls on one boot, five on a
new protection boot, and nine fresh-boot sham-return calls. One failed
native client call is retained and excluded from valid measurements;
the two protection boots are reported separately, not pooled as a single
nine-sample boot. Their losses against the two-sham mean were 11.06% and
11.69%, with a −1.33% sham-return drift.

BPF-path and return-mispredict observations were made in separate
diagnostic runs. Do not collect those probes during the clean timing.
The native parent/child investigation used baseline anchors; do not
describe the 13.59% child-versus-anchor result as a direct parent/child
percentage. The main result table instead reports the separately completed
v7.1.3 / v7.3 / v7.1.3 comparison.
