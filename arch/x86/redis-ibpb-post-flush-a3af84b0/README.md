# Redis ZPOPMIN: persistent cost after an IBPB flush

**Origin:** native MMTests Redis, not model-generated workloads. This is the
Redis part of [R07](../../../candidate/additional-review/README.md#r07-related-external-redis-evidence-2026-11-01),
not another independent issue.

Status, November 7, 2026: the original report was sent on November 3.
The direct-commit CPU profiles and microcode comparison were sent in reply
to Dave Hansen on November 4; sent contents and reply headers were checked.
The exact public archive locations of those messages have not been verified.
The bounded follow-up is closed for now, without a proposed mitigation change
or a complete explanation of the processor's persistent prediction state.

## Confirmed result

Bisection and separate baseline-anchored measurements identified
[`a3af84b0fa00`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/a3af84b0fa00ead01fcd0e28b5d773ff25990a0d),
"x86/bugs: Enable IBPB flush on BPF JIT allocation"; its parent is
`96cce16e26dd02a8678f1e87f88a4b5cdb63b995`.
The release comparison below is separate from that attribution.

Unmodified Redis 6.2.6 on an i7-12700KF, microcode 0x3e; one client,
pipeline depth 256. Each of three fresh boots had nine clean measured
calls, each containing five repetitions of the complete 20-command recipe.

| Metric | v7.1.3 A | v7.3 | v7.1.3 B | v7.3 vs. baseline mean |
| --- | ---: | ---: | ---: | ---: |
| ZPOPMIN, requests/s | 999,365.83 | 871,396.84 | 1,000,946.61 | −12.87% |
| GET, requests/s | 3,548,521.06 | 3,551,131.59 | 3,547,932.59 | +0.08% |
| Complete test lifecycle, seconds | 61.396 | 61.704 | 61.301 | +0.58% |

ZPOPMIN loses about 128,759 requests/s. Its within-boot CV is 0.67–0.75%;
baseline return drift is +0.16%. The lifecycle includes server startup,
all commands and repetitions, synchronization and cleanup. This is not
a 13% slowdown of Redis as a whole.

## Why the post-flush state matters

The same commit has an earlier
[stress-ng.seccomp report](https://lists.openwall.net/linux-kernel/2026/08/19/369)
that repeatedly loads filters. No BPF JIT allocation or associated IBPB
execution was observed during our native Redis measurement window.

On the unchanged fast parent, short-lived helpers enabled indirect-branch
protection for themselves on CPUs 0–19 and exited before Redis started.
Redis did not inherit that setting. Two independent protection boots
reproduced ZPOPMIN losses of 11.06% and 11.69% against fresh-boot sham
controls; sham return drift was −1.33%. Complete-test time rose by only
0.35% and 0.17%. Separate return-misprediction diagnostics reproduced the
slow kernel's pattern: sampled server RET mispredictions rose from 0/1
to 624/626. The protection action had finished before the affected test.

The existing pack-selection optimization `a9b1f19a6a67` is already in
tested v7.3. An RSB-fill diagnostic retaining IBPB did not recover the
throughput. Neither finding justifies removing the security protection.

## Upstream-requested microcode and CPU-profile checks

Pawan Gupta asked for a same-kernel comparison with another microcode
revision. A fresh control / protection-trigger / control sequence, nine
clean calls per boot, reproduced the loss on both revisions:

| Microcode | ZPOPMIN throughput change | Complete-test time change |
| --- | ---: | ---: |
| 0x3e | −12.03% | +0.27% |
| 0x37 | −12.14% | +0.24% |

All 54 calls and 270 native CSVs passed independent replay. Absolute
control return drift stayed below 0.19%; maximum within-boot CV was 1.40%.
The temporary `dis_ucode_ldr` boots were verified on all 20 CPUs and
reported the expected old-microcode taint; other reported protection
settings matched. Normal 0x3e operation was restored. No BIOS was flashed.

Dave requested profiles showing where the additional CPU work occurs.
The direct parent / child / parent comparison used the existing images,
normal 0x3e and no extra protection trigger. Each boot had one unprofiled
call followed by two CPU-clock profiles at 1999 Hz.

Counts below average the two profiles per boot. Each profile sums the
five native windows for that command; these are diagnostic sample counts,
not the rates or timings from the clean table above.

| Command / execution location | Parent A | Child | Parent B |
| --- | ---: | ---: | ---: |
| ZPOPMIN, server kernel | 581.0 | 673.0 | 559.5 |
| ZPOPMIN, client kernel | 381.5 | 428.0 | 376.5 |
| ZPOPMIN, server user | 408.5 | 442.0 | 407.0 |
| ZPOPMIN, client user | 103.5 | 136.0 | 97.5 |
| ZPOPMIN, total | 1474.5 | 1679.0 | 1440.5 |
| GET, total | 286.5 | 291.0 | 293.0 |

About 69% of the added ZPOPMIN samples are in kernel code, especially on
the server. Increases are distributed across syscall and network paths,
including `do_syscall_64()`, `tcp_sendmsg_locked()`,
`__tcp_transmit_skb()` and `skb_release_data()`, rather than one dominant
new hotspot. The [729-row symbol comparison](bare-metal/commit-cpu-profiles.csv)
retains both controls, negative differences and unknown symbols.

Dave's November 4 reply did not offer a clear optimization and suggested
further investigation. A final bounded CPU-locality experiment suggested
greater sensitivity on the server core, but stopped after native client
errors before a final return control. Its valid prefix is not promoted
to a confirmed locality or processor-internal explanation. Further Redis
experiments are deferred.

## Reproduction

Use the complete native command sequence:

```text
redis-benchmark --csv -c 1 -r 50000 -n 100000 -P 256
```

ZADD populates the sorted set before ZPOPMIN, which drains it without
refilling. The requests therefore include empty results, not just
successful removals. Do not replace the recipe with `-t zpopmin`.

See [the reproduction steps and tested helper](reproducer/README.md) and
[中文说明](README.zh-CN.md). Full raw profiles, failed runs and correspondence
remain local; the CSV above is a curated symbol summary without private paths.
