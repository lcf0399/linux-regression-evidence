# tmpfs flistxattr(fd) Small-List Regression Evidence

Closed on 2026-10-10. No further experiments or follow-up are queued.
The corresponding fix is in mainline. Report and validation evidence are retained;
dedicated experimental kernels and rebuildable artifacts may be retired.

Timeline (UTC+08:00): fix `1e7cd8a53b72` entered Linus's tree on June 15,
2026, through merge [`ff8747aacaff`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux.git/+/ff8747aacaff8266dd751b8a8648fb728dcc3b21),
before our July 6 report. The tested 7.1.0 release and later 7.1.3 check lacked
that fix. Jan requested a retest on July 9; our July 17 reply provided exact
parent/child bare-metal validation. This was independent reproduction in the
released 7.1 line and validation of an existing mainline fix, not a fix written
or first merged in response to our report.

Release containment checked on 2026-10-10: the fix is an ancestor of the
[v7.2 release](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux.git/+/refs/tags/v7.2)
(`8d3ae59288f1`, released August 17, 2026, UTC+08:00), but not v7.1.
The v7.2 source still contains the per-superblock cache and list-based enumeration.
Thus v7.2 is the first mainline final release containing this fix. This check does
not establish whether, or when, it was backported to older stable branches, and
does not add a new performance measurement.

This directory contains curated evidence for a narrow Linux FS performance
regression:

```text
tmpfs + user xattrs + flistxattr(fd) + small xattr lists
```

The strongest result is a bare-metal parent/child A/B around:

```text
52b364fed6e1 shmem: adapt to rhashtable-based simple_xattrs with lazy allocation
```

The parent already has the rhashtable-based simple-xattr infrastructure, but
tmpfs still uses the old rbtree path.  The child switches tmpfs to the lazy
rhashtable-based simple-xattrs path.

With one xattr, skipping the first warmup round:

| kernel / state | mean ns/op |
| --- | ---: |
| parent, tmpfs old rbtree path A | 135.754 |
| child, tmpfs lazy rhashtable path | 229.663 |
| parent, tmpfs old rbtree path B | 135.335 |

The child is about `+69.4%` slower than the average of the two parent runs.
All runs reported `expected_match_ratio=100` and `unexpected_results=0`.

A matched production-like A/B with `SLUB_DEBUG` and `KASAN` disabled still shows
the child about `+50.4%` slower (`223.866 ns/op` vs `148.857 ns/op` parent
average).  `CONFIG_DEBUG_KERNEL` remained enabled because this config has
`CONFIG_EXPERT=y`, which selects `DEBUG_KERNEL`.

The count-gradient run shows that the signal is strongest for small xattr
lists and is amortized as the list grows:

| xattr count | child vs parent average |
| ---: | ---: |
| 1 | +55.2% |
| 4 | +49.5% |
| 16 | +20.3% |
| 64 | +6.4% |

## Upstream Fix Follow-up

An exact bare-metal parent/child validation shows that
`1e7cd8a53b72 ("simpe_xattr: use per-sb cache")` fully removes the measured
slowdown in this workload.  The child was `35.93%` faster than the midpoint of
its direct-parent controls and `4.21%` faster than the Linux 7.0.14 control
midpoint.  The updated Linux 7.1.3 stable point, which does not contain the
commit, remained `54.47%` slower than the Linux 7.0.14 midpoint.

The released-kernel point in the original report was Linux 7.1.0
(`7.1.0-bm-7.1`).  The Linux 7.1.3 point is an updated stable-line check, not
the original report point or the patched kernel.  See `fix-validation/` for
the compact six-boot validation evidence.

## Scope

This is a small-list tmpfs `flistxattr(fd)` report.  It is not a claim that all
xattr operations, all tmpfs xattr workloads, or all rhashtable-backed xattr
workloads are slower.

The diagnostic source probes in the local investigation point toward
`shmem_listxattr()` / `simple_xattr_list()` and the rhashtable walk fixed cost,
but they are attribution evidence only.  They are not proposed upstream
patches or reverts.

## Contents

- `reproducer/xattr_smoke.c`: standalone workload source used by the xattr
  experiments.
- `bare-metal/`: citable bare-metal release-window, exact A/B,
  production-like A/B, and count-gradient summaries.
- `fix-validation/`: exact parent/child validation of the upstream per-sb
  cache fix, including the updated Linux 7.1 stable check.
- `attribution/`: compact attribution summaries, including bpftrace layering
  and diagnostic source-probe outcomes.
