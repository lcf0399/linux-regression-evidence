# mprotect Shared-dirty Workload Source

This directory contains the generated userspace workload source used by the
`mprotect_shared_dirty_formal_refresh` profile.

The reportable scenario is:

- `shared_dirty_full_toggle_64m`
- anonymous shared 64 MiB mapping
- prefault before protection changes
- full-range `mprotect(PROT_READ)` followed by restore to `PROT_READ | PROT_WRITE`
- write-touch after the protection cycle

This source is included for auditability of the workload semantics. Public
timing evidence is limited to the physical-machine results in `../bare-metal/`;
the standalone maintainer reproducer is in `../reproducer/`.
