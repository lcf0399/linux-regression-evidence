# 2026-07-02 cac1db8c3aad revert attempt

This directory records a follow-up attempt to move from attribution probe
toward a cleaner commit-level check for:

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

## What was attempted

I fetched the `v6.16` and `v6.17` tags into the local `linux-mm-unstable` git
repository and created a temporary worktree at `v6.17`.

Running a direct revert:

```bash
git revert --no-commit cac1db8c3aad
```

did **not** apply cleanly even on the real `v6.17` tag.  The conflict was in
`mm/mprotect.c`.

The relevant follow-up commits in `v6.16..v6.17` touching `mm/mprotect.c` after
`cac1db8c3aad` are:

```text
cf1b80dc31a1 mm: pass page directly instead of using folio_page
8b2914162aa3 mm/mseal: small cleanups
```

So the earlier reverse-apply failure was not just a stale local snapshot issue:
the release tag already has later edits layered on top of the batching change.

## Candidate mprotect-only minus-cac patch

Because `cac1db8c3aad` only changes `mm/mprotect.c`, I synthesized a
`v6.17` mprotect-only candidate by taking the `v6.17` tree and replacing
`mm/mprotect.c` with the pre-`cac1db8c3aad` shape plus later compatible
`mm/mprotect.c` cleanup context.

Patch:

```text
0001-v6.17-minus-cac1db8c3aad-mprotect-only-candidate.patch
```

This patch removes the batching commit/flush machinery from the `v6.17`
`change_pte_range()` present-PTE path.  It is closer to a `v6.17` minus-cac
candidate than the earlier single-PTE probe, but it is still a synthesized
mprotect-only candidate rather than a clean exact `git revert`.

## Build check

The candidate tree passed both a local object-level build check and a full
`bzImage` build:

```bash
make -C linux-kernel-trees/sources/linux-v6.17-minus-cac-build olddefconfig
make -C linux-kernel-trees/sources/linux-v6.17-minus-cac-build -j$(nproc) mm/mprotect.o
make -C linux-kernel-trees/sources/linux-v6.17-minus-cac-build -j$(nproc) bzImage
```

Result:

```text
mm/mprotect.o build passed
arch/x86/boot/bzImage build passed
kernelrelease: 6.17.0-dirty
```

The same candidate was then built and installed on the bare-metal node as:

```text
6.17.0-bm-6.17-minus-cac1db8c3aad
```

`/boot` stayed below the local 90% safety threshold after install.

## Bare-metal timing

I ran the same shared-dirty full-toggle reproducer queue on the bare-metal
node, interleaving three kernels for three rounds:

```text
6.16 clean
6.17 clean
6.17 mprotect-only minus-cac candidate
```

Main metric: `iteration_ns_per_page`, lower is better.

| Kernel | values | mean |
| --- | --- | ---: |
| `6.16.0-bm-6.16` | 25 25 25 | 25.000 |
| `6.17.0-bm-6.17` | 38 36 36 | 36.667 |
| `6.17.0-bm-6.17-minus-cac1db8c3aad` | 27 27 26 | 26.667 |

All steps reported:

```text
expected_match_ratio=100
unexpected_results=0
smaps_*_kernel_page_kb=4
smaps_*_mmu_page_kb=4
smaps_*_anon_huge_kb=0
```

So this synthesized mprotect-only minus-cac candidate brings the workload from
the `v6.17` slow range back very close to the `v6.16` fast range.

This is stronger attribution evidence than the earlier single-PTE probe, but
it should still be described carefully:

- direct `git revert cac1db8c3aad` conflicts on `v6.17`;
- the tested tree is a hand-synthesized `mm/mprotect.c`-only minus-cac
  candidate;
- therefore this is not a clean exact-revert A/B proof, even though the timing
  result strongly points at the batching change as the relevant mechanism for
  this workload.

## Files

- `0001-v6.17-minus-cac1db8c3aad-mprotect-only-candidate.patch`: tested
  mprotect-only candidate patch.
- `build-check.txt`: local build-check note.
- `revert-attempt-summary.csv`: direct-revert/build status summary.
- `step-summary.csv`: per-step bare-metal timing results.
- `aggregate-summary.csv`: aggregate bare-metal timing means.
- `raw/mprotect_minus_cac_20260702/`: copied text logs, summaries, and run
  environment from the bare-metal queue.
