# Bare-metal result

The primary table is an independent run of the concise standalone source in
this bundle. Each point used a fresh boot and 3 warm-up plus 15 measured
rounds. Each measured round sent 65,536 messages as 2,048 `sendmmsg()` calls.
Only the syscall was timed; setup, peer drain, and validation were not.

The experiment pair isolates the exact source delta of upstream
`6456ccbd2ff7` on one synthetic merge baseline:

- parent `0bfa1c2da7a88d4e5fcd65044a3076f8d672f3a4`;
- child `30cb02a874b4c62191decf55c3971e45a5173d50`;
- order: parent A, child, parent B.

| source/profile | actual preempt | parent A | child | parent B | child vs midpoint | drop-first | parent drift |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| standalone `sendmmsg()` | none | 343.382 | 395.831 | 343.259 | `+15.295%` | `+15.289%` | `-0.036%` |
| formal `sendmmsg()` | full | 341.915 | 393.803 | 343.910 | `+14.841%` | `+14.863%` | `+0.584%` |
| formal io_uring SEND | full | 314.070 | 373.614 | 319.424 | `+17.953%` | `+17.939%` | `+1.705%` |

Units are ns/message or ns/op and lower is better. The two sources use
different matched runtime preemption modes and are analyzed separately. The
standalone maximum CV was `0.137%`; the formal maximum CV was `0.154%` for
direct `sendmmsg()` and `0.110%` for io_uring SEND. All selected rows passed
their semantic checks.

As supporting path evidence, a separate v6.16.12 A -> v6.17.13 -> v6.16.12 B
`perf` sandwich reported `security_unix_may_send()` children overhead of
`0.27%`, `6.77%`, and `0.27%`, respectively. At the new point, the sampled
stack continued through `apparmor_unix_may_send()`, `aa_unix_peer_perm()`, and
`unix_peer_perm()`. This release-endpoint observation is not mixed with the
exact-pair timing table and is not an internal ablation.

The machine was an Intel Core i7-12700KF system with 32 GiB RAM. Every point
was pinned to P-core CPU 2, with `intel_pstate` governor and EPP set to
`performance` and Turbo disabled. All formal points used the same normalized
kernel config, GCC 15.2.0 toolchain, Kbuild metadata, module-signing key, and
workload binary. The standalone binary was also identical across its three
boots. Its source SHA-256 was
`71544093a1b948404ed186f36870515867602b2c9696207dea6e6238c5904704`;
the binary SHA-256 recorded in `run-identity.tsv` was
`42f90b0ea82867ad10cf8aa2101d5079194e620081795de2f1d946ac408f4d80`.

The formal source contains several io_uring profiles, but its `u0` control
times only `sendmmsg()` and drains the peer outside timing. The shorter source
removes io_uring setup entirely and independently reproduces that result.
