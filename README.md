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

## Evidence Policy

- Keep only curated summaries, standalone reproducers, compact CSV/JSON
  summaries, and small attribution probes needed to understand the claim.
- Do not upload private mail drafts, failed scratch logs, bulky raw runner
  workspaces, or local-only archives.
- Prefer immutable commit links when referencing this repository from upstream
  email.
- State workload scope and caveats directly; do not present a narrow
  source-calibrated workload as a generic subsystem regression.
