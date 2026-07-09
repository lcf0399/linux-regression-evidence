# 2026-07-09 userfaultfd bulk writeprotect bridge

This is related mechanism evidence for the mprotect shared-dirty regression
thread.  It is not a separate `mm/userfaultfd.c` regression claim.

The question was whether a different entry point into the same PTE permission
change machinery shows the same v6.16 -> v6.17 cost increase.

## Workload

- Scenario: `bulk_writeprotect_ioctl_1024m`
- Mapping: 1 GiB anonymous mapping
- Operation: register a userfaultfd write-protect range, then run bulk
  `UFFDIO_WRITEPROTECT` set/clear over the full range.
- Pinning: CPU 2
- Runs: 5 interleaved bare-metal batches per kernel

## Kernels

- `6.16.0-bm-6.16`
- `6.17.0-bm-6.17`
- `6.17.0-bm-6.17-minus-cac1db8c3aad`

The `minus-cac1db8c3aad` kernel is a hand-adapted mprotect-only mechanism
candidate for the PTE permission-change path.  It is not a clean exact
`git revert` and not a proposed fix.

## Result

Primary metric: `protect_ns_avg / pages`, in ns/page.

| kernel | precise ns/page samples | mean | min | max | integer samples |
| --- | ---: | ---: | ---: | ---: | --- |
| `6.16.0-bm-6.16` | 26.361, 23.196, 26.622, 26.142, 26.277 | 25.720 | 23.196 | 26.622 | 26, 23, 26, 26, 26 |
| `6.17.0-bm-6.17` | 32.688, 32.703, 34.102, 34.139, 34.087 | 33.544 | 32.688 | 34.139 | 32, 32, 34, 34, 34 |
| `6.17.0-bm-6.17-minus-cac1db8c3aad` | 27.410, 27.368, 24.161, 27.265, 23.996 | 26.040 | 23.996 | 27.410 | 27, 27, 24, 27, 23 |

Relative to the `6.16` mean:

- `6.17`: about `+30.4%`
- `6.17-minus-cac1db8c3aad`: about `+1.2%`

All 15 runs reported:

```text
expected_match_ratio=100
unexpected_results=0
errno_eperm=0
errno_einval=0
errno_enoent=0
errno_enomem=0
errno_eexist=0
errno_other=0
```

## Interpretation

The `6.17` slowdown in this userfaultfd bulk write-protect workload is mostly
removed by the same mprotect/PTE permission-change mechanism candidate that
also pulls the shared-dirty mprotect workload back toward the v6.16 range.

This supports treating the userfaultfd result as an additional entry-point
check for the same `change_protection()` / PTE update mechanism, not as an
independent `mm/userfaultfd.c` regression.

Because the `minus-cac1db8c3aad` kernel is not a clean exact revert, this does
not prove a single culprit commit.
