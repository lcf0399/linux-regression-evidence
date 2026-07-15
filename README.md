# Linux Regression Evidence

This repository is a curated public evidence bundle for Linux performance
regressions that still look actionable after follow-up validation.

## Current Evidence

- `mprotect-shared-dirty-toggle/`

  A narrow Linux MM `mprotect()` workload on a shared-dirty 4 KiB base-page
  mapping.  Bare-metal runs narrow the slowdown to the `v6.16 -> v6.17`
  release window.  A focused v6.17 single-PTE source probe brings the result
  back to the v6.16 range for this workload, and a later single-protect
  follow-up shows that one `mprotect(PROT_READ)` on a prepared shared-dirty
  range already reproduces the slowdown.

  Scope: source-calibrated shared-dirty PTE workload, not a generic
  `mprotect()` regression claim.

- `tmpfs-flistxattr-small-list/`

  A narrow Linux FS `flistxattr(fd)` workload on tmpfs files with small
  `user.*` xattr lists.  Bare-metal parent/child A/B around
  `52b364fed6e1 shmem: adapt to rhashtable-based simple_xattrs with lazy
  allocation` shows that the tmpfs switch from the old rbtree path to lazy
  rhashtable-based `simple_xattrs` increases the small-list fixed cost.

  Scope: tmpfs `flistxattr(fd)` with small xattr lists, not a generic xattr or
  tmpfs regression claim.

- `btrfs-remap-writeback-inhibition-v2/`

  Independent bare-metal validation of the upstream v2 patch that replaces
  the per-transaction writeback-inhibition xarray with a fixed inline buffer.
  In a matched control/patch/control sandwich, the patch reduced the mean cost
  of 4 KiB Btrfs `FICLONERANGE` by about `27.0%` and `FIDEDUPERANGE` by about
  `22.0%` for the included micro-workload.

  Scope: Btrfs on a brd-backed filesystem with a narrow 4 KiB clone/dedupe
  micro-workload, not a generic remap-range or application-level performance
  claim.

## Evidence Policy

- Keep only curated summaries, standalone reproducers, compact CSV/TSV/JSON
  summaries, and small attribution probes needed to understand the claim.
- Do not upload private mail drafts, failed scratch logs, bulky raw runner
  workspaces, or local-only archives.
- Prefer immutable commit links when referencing this repository from upstream
  email.
- State workload scope and caveats directly; do not present a narrow
  source-calibrated workload as a generic subsystem regression.
