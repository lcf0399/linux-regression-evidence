# Bare-Metal Results

All timing evidence here comes from bare-metal runs.  Local/QEMU raw data is not
used for the upstream-facing claim.

## Release Window

Scenario:

```text
XATTR_SCENARIO=flistxattr_fd
TEST_DIR=/tmp
ITERATIONS=65536
EXTERNAL_ROUNDS=15
PIN_CPU=2
```

The table uses skip-first-3 mean `ns_per_op`.

| label | kernel | mean ns/op | comparison |
| --- | --- | ---: | --- |
| v612a | 6.12.77-bm-6.12.77 | 162.250 | early baseline |
| v61819 | 6.18.19-bm-6.18.19 | 132.786 | fast range |
| v6199 | 6.19.9-bm-6.19.9 | 134.794 | fast range |
| v71 | 7.1.0-bm-7.1 | 220.360 | slow range |
| v612b | 6.12.77-bm-6.12.77 | 135.226 | old-version recheck |

Key deltas:

| comparison | delta |
| --- | ---: |
| v6.19.9 vs v6.18.19 | +1.51% |
| v7.1 vs v6.19.9 | +63.48% |
| v7.1 vs v6.12 recheck | +62.96% |

## Exact Parent/Child A/B

Compared commits:

| role | commit | relevant state |
| --- | --- | --- |
| parent | b32c4a213698 | rhashtable infrastructure exists; tmpfs still uses old rbtree path |
| child | 52b364fed6e1 | tmpfs switches to lazy rhashtable-based simple xattrs |

Scenario:

```text
XATTR_SCENARIO=flistxattr_fd
filesystem=tmpfs
rounds=15
iterations=65536
pin_cpu=2
order=parent -> child -> parent
```

All runs reported `expected_match_ratio=100` and `unexpected_results=0`.
The table uses skip-first-1 mean.

| target | mean ns/op |
| --- | ---: |
| parent A | 135.754 |
| child | 229.663 |
| parent B | 135.335 |

The child is about `+69.4%` slower than the average of the two parent runs.

Skip-first-1 ranges and standard deviations:

| target | min ns/op | max ns/op | stdev ns/op |
| --- | ---: | ---: | ---: |
| parent A | 134.451 | 140.153 | 1.742 |
| child | 217.758 | 239.498 | 7.126 |
| parent B | 134.273 | 138.098 | 1.048 |

The parent and child kernels were built with the same GCC 13.3 toolchain and
their `.config` files differ only in `CONFIG_LOCALVERSION`.  `CONFIG_TMPFS_XATTR`
was enabled, `CONFIG_KASAN` was disabled, and the configs had
`CONFIG_DEBUG_KERNEL=y` / `CONFIG_SLUB_DEBUG=y`.

## Count Gradient

Scenario:

```text
XATTR_SCENARIO=flistxattr_fd_count
XATTR_COUNT=1/4/16/64
rounds=9
iterations=65536
order=parent counts -> child counts -> parent counts
```

This scenario checks that the returned list length matches the exact expected
length for the seeded xattr names.

| xattr count | parent A mean ns/op | child mean ns/op | parent B mean ns/op | child vs parent avg |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 156.102 | 242.886 | 156.872 | +55.2% |
| 4 | 185.109 | 273.756 | 181.206 | +49.5% |
| 16 | 312.894 | 378.211 | 315.962 | +20.3% |
| 64 | 885.222 | 934.356 | 870.759 | +6.4% |

This supports the narrower interpretation that the regression is mostly a
small-list fixed-cost increase.

## Production-Like Config A/B

I also reran the same parent/child/parent A/B after disabling the heavier debug
options that could plausibly amplify allocator or traversal overhead:

```text
# CONFIG_SLUB_DEBUG is not set
# CONFIG_KASAN is not set
```

`CONFIG_DEBUG_KERNEL` remained enabled because this config has `CONFIG_EXPERT=y`,
and upstream Kconfig makes `EXPERT` select `DEBUG_KERNEL`.  Disabling `EXPERT`
would change a wider set of kernel semantics, so this run should be interpreted
as a matched `no SLUB_DEBUG / no KASAN` production-like A/B, not as a fully
`DEBUG_KERNEL=n` build.

Scenario:

```text
XATTR_SCENARIO=flistxattr_fd
filesystem=tmpfs
rounds=15
iterations=65536
pin_cpu=2
order=parent -> child -> parent
```

All runs reported `expected_match_ratio=100` and `unexpected_results=0`.
The table uses skip-first-1 mean.

| target | mean ns/op | min ns/op | max ns/op | stdev ns/op |
| --- | ---: | ---: | ---: | ---: |
| parent A | 148.536 | 147.100 | 153.348 | 1.458 |
| child | 223.866 | 215.126 | 235.209 | 5.846 |
| parent B | 149.178 | 148.350 | 150.778 | 0.556 |

The child is about `+50.4%` slower than the average of the two parent runs
(`223.866 ns/op` vs `148.857 ns/op`, about `+75.0 ns/op`).

This confirms that the small-list slowdown is still visible after disabling
the heavy SLUB/KASAN debug options.  The absolute delta is smaller than in the
earlier debug-config A/B, but the direction and magnitude remain clear.
