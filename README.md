# Linux Regression Evidence

This repository is a curated public evidence bundle for Linux performance
regressions and upstream follow-up or patch validation.

## Current Evidence

- `mprotect-shared-dirty-toggle/`

  A narrow Linux MM `mprotect()` workload on a shared-dirty 4 KiB base-page
  mapping. Bare-metal runs first narrowed the slowdown to the
  `v6.16 -> v6.17` release window. A later exact direct-parent/child sandwich
  identifies `cac1db8c3aad ("mm: optimize mprotect() by PTE batching")` as the
  source of the measured signal: the child was `39.77%` slower than the parent
  midpoint, with only `0.87%` parent drift.

  Scope: source-calibrated shared-dirty PTE workload, not a generic
  `mprotect()` regression claim.

- `tmpfs-flistxattr-small-list/`

  A narrow Linux FS `flistxattr(fd)` workload on tmpfs files with small
  `user.*` xattr lists.  Bare-metal parent/child A/B around
  `52b364fed6e1 shmem: adapt to rhashtable-based simple_xattrs with lazy
  allocation` shows that the tmpfs switch from the old rbtree path to lazy
  rhashtable-based `simple_xattrs` increases the small-list fixed cost.

  Follow-up exact parent/child testing shows that
  `1e7cd8a53b72 ("simpe_xattr: use per-sb cache")` removes the measured
  slowdown: the child was about `35.9%` faster than its direct parent and
  about `4.2%` faster than the Linux 7.0.14 control midpoint.

  Scope: tmpfs `flistxattr(fd)` with small xattr lists, not a generic xattr or
  tmpfs regression claim.

- `fsnotify-concurrent-inotify-watch-setup/`

  An exact three-boot parent/child/parent A/B attributes a repeatable P8
  distinct-inode inotify watch add/remove slowdown to
  `94bd01253c3d fsnotify: Track inode connectors for a superblock`. The child
  was about `15.1%` to `21.6%` slower across the absolute and matched paired
  metrics. A second exact scaling sandwich found the first tested stable material
  point at P6 (`9.0%` to `10.9%`) and a stronger same-window P8 signal
  (`16.8%` to `19.6%`); P1/P4 remained below the signal gate. Same-commit
  probes narrow the cost to per-superblock list mutation or its added lock
  hold time, plus lock handoff. A separate stock-`inotifywait` trace shows that
  eight recursive watchers on a real Linux source tree create the same
  multi-process, distinct-inode connector topology.

  A 2026-07-20 source and prior-art audit checked `v7.1.4`, `v7.2-rc4`,
  Linus' tip, linux-next, and the linux-fs maintainer branches. The introduced
  lock/list operations remain present, and no equivalent fix or existing
  regression report was found. This is not a claim that the exact A/B
  percentage was remeasured on the latest tip.

  Scope: concurrent distinct-inode watch setup/teardown around the exact
  commit. The real-software trace is a topology gate, not application timing,
  and the evidence does not suggest reverting the sparse-unmount optimization.

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
