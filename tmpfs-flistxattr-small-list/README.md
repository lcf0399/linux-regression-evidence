# tmpfs flistxattr(fd) Small-List Regression Evidence

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
- `attribution/`: compact attribution summaries, including bpftrace layering
  and diagnostic source-probe outcomes.
- `email/`: local upstream email drafts and checklist.  This directory is
  intentionally ignored by git in this repository.
