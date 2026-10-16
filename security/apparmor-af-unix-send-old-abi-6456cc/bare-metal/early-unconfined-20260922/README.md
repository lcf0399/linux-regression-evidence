# AppArmor early-return prototype: September 21–22, 2026

On the fixed v7.3-rc4 baseline, the prototype reduced unconfined socketpair
send time by **12.84%**. A separate two-process test measured a **1.25% cost**
when both peers were confined but allowed to communicate. Completed permission
tests found no difference from the original kernels. This is a prototype, not
an exhaustive security validation or an application-level performance claim.

## Prototype comparisons

Both comparisons used independent original A → patched B → original A2 boots.
The baseline was `93f51579e7df248780214094418f205253383cc5` (v7.3-rc4).
Both kernels used that baseline, GCC 15.2.0 and the same configuration except
for `CONFIG_LOCALVERSION`; the only source change was the prototype.
The [patch](early-unconfined.patch) is byte-identical to the tested patch.
It checks the whole label in `aa_unix_peer_perm()`, keeps separate sender and
receiver decisions, and adds no per-object fields.

All values below are ns/message; lower is better. Changes use the midpoint of
the two original boots. The two workload shapes are analyzed separately.

| Workload / peer confinement | Original A | Patched | Original A2 | Change |
| --- | ---: | ---: | ---: | ---: |
| Socketpair, unconfined | 416.073 | 359.448 | 408.719 | −12.84% |
| Two processes, both unconfined | 455.537 | 412.052 | 455.592 | −9.55% |
| Two processes, sender only | 569.749 | 561.786 | 570.054 | −1.42% |
| Two processes, receiver only | 576.514 | 562.807 | 579.974 | −2.67% |
| Two processes, both confined | 740.810 | 750.620 | 741.959 | +1.25% |

The socketpair saving was 52.95 ns/message. Its maximum within-invocation CV
was 0.114%, and original-boot drift was 1.78%. In the two-process test the
corresponding maxima were 1.80% and 0.60%. The both-confined case added
9.24 ns/message, with CV ≤0.515% and drift 0.155%; dropping the first measured
round of every invocation still gave +1.255%. The main results retain all
measured rounds. An effect below 5% is not treated as zero cost.

## Workload and machine

- Intel Core i7-12700KF, 32 GiB RAM, 20 logical CPUs online; full preemption,
  performance governor/EPP, Turbo disabled. Sender CPU 2; two-process receiver
  CPU 4, a different physical P-core.
- Socketpair: the existing [standalone source](../../reproducer/af_unix_sendmmsg_standalone.c),
  `socketpair(AF_UNIX, SOCK_DGRAM)`, unconfined.
- Two processes: connected abstract AF_UNIX datagram sockets. Each process
  enters its single policy label before dropping to UID/GID 1000, clearing
  supplementary groups and setting `no_new_privs`; sockets are created after
  the privilege drop. Confined timing policies allow communication.
- Each condition: 3 invocations per boot, each with 3 warm-up and 15 measured
  rounds; 65,536 messages per round, 32 per batch, 128 bytes each.
- Only `sendmmsg()` is timed. Preparation, receiver draining, payload checks and
  pipe coordination are excluded. Untimed function probes run separately.

## Correctness and direct hits

Each of the six prototype-comparison boots passed 29 static controls, including
mixed peers, stacked labels, datagram/stream sockets and old/new policy ABIs.
Fine-grained UNIX controls used ABI 5.0 (`network_v9`); old-ABI controls used
`kernel-5.4-vanilla`.

The two-process follow-up added 84 fixed-phase checks per boot with the same
sockets held across policy replacements. All three boots made the same
decisions, with no skipped phases or pending packets at close. Each boot also
ran two two-second concurrency checks, each overlapping 16 policy replacements.
Sender-policy changes produced both allows and denials; a fixed sender deny
never allowed a send while the receiver policy changed.

Receiver-only deny updates still allowed delivery through the existing socket
on both original boots and the prototype. This path uses the receiver socket's
stored label: the test establishes baseline equivalence, **not immediate
revocation**. Live updates of stacked labels, namespace variations, all socket
types and every possible concurrent interleaving were not covered.

In the socketpair probe, `apparmor_unix_may_send` / `aa_unix_peer_perm` counts
were 1,056 / 2,112 at each boot; internal `unix_peer_perm` counts were
2,112 → 0 → 2,112. Two-process probe windows sent 1,024 messages and recorded
1,024 / 2,048 outer calls in all cases. Internal calls were 2,048 on the original
kernel; on the prototype they were 0 / 1,024 / 1,024 / 2,048 in table order.
All instrumented windows were excluded from clean timing.

## Separate original-regression retest

An earlier four-boot run used the same standalone source, sampling and full
preemption: old-fast A → old-slow → v7.3-rc4 → old-fast B.

| Point | Mean ns/message |
| --- | ---: |
| Old-fast A (`0bfa1c2da7a8`) | 342.422 |
| Old-slow (`30cb02a874b4`) | 390.530 |
| v7.3-rc4 (`93f51579e7df`) | 413.677 |
| Old-fast B | 345.043 |

The controlled source delta of `6456ccbd2ff7` remained +13.61%; the whole-release
comparison was +20.35%. The latter includes other source/configuration changes
and cannot all be attributed to that commit. Maximum pooled CV was 0.264%,
and old-fast B/A drift was +0.765%. These results are not combined with the
prototype comparisons or the earlier report's preempt=none measurements.

## Files and verification

- [summary.tsv](summary.tsv): seven comparisons, drift, CV and drop-first checks.
- [samples.tsv](samples.tsv): all 855 measured rows from the three experiments,
  plus 108 retained warm-up rows from the two-process test. The socketpair
  program performs warm-ups but emits only measured rows.
- [run-identity.json](run-identity.json): source/build identities, ten distinct
  boots, runtime settings and binary identities.
- [validation.json](validation.json): curated untimed outcomes, exact probe
  counts, concurrency counts and retained preflight-failure explanations.
- [reproducer/](reproducer/README.md): unchanged native workload and permission
  helpers. Original large logs, builds and failed-attempt directories remain
  in the experiment archive, not duplicated here.

Run `python3 verify.py` from this directory to independently recalculate the
tables from elapsed times and check sample inventories, boot identities and
packaged source/patch hashes. It does not rerun live permission tests.
The raw-output audits were also rerun before preparing this bundle.

CV uses sample standard deviation / mean. Prototype drift is
`abs(A2 - A) / midpoint`; the archived regression retest uses `(B / A - 1)`.
All warm-ups are excluded from reported means. Each invocation contributes
15 measured rows; no row is selected by performance. The machine was restored
to its generic kernel and original power/boot settings after the experiments.
