# Upstream source snapshots

Read-only branch refresh on 2026-10-18 at 01:09 (UTC+08:00):

| Tree | Exact head |
| --- | --- |
| Linus master | `58161ead29e50d70a317ea26b472e0c7508e69da` |
| akpm/mm-stable | `2967f3ccdb5fae956f1b085370ec292bab5c7e25` |
| akpm/mm-unstable | `b306deee4cba9b6965a31b7d644f2a92574cd322` |

The reviewed `vmscan.c` and `huge_memory.c` snapshots have no equivalent
reclaim-local reordering. The fixed mm-stable baseline is unchanged, and
the original patch still applies without fuzz. These findings concern the
listed source snapshots; only the fixed mm-stable baseline and its patched
counterpart were runtime-tested for the results in this bundle.
