# Non-timing evidence and the local root-reuse prototype

The introducing changes repair notification and concurrency semantics.
Their purpose does not establish a latency budget, nor prove every additional
operation is unavoidable. No revert or unconditional lookup bypass is proposed.

`root-reuse.patch` is the exact local prototype tested on
`eea5d2bb34ba11dccd9c53f392dc50cf060150a9`. It obtains the kernfs root once
per internal removal call and passes it to the link-count helper. It retains
the locks and their ordering, inode lookups, link-count clearing, notifications,
timestamps and reference operations. It does not cache a mutable parent or
superblock list across a lock drop.

The original tree is `e2368bca2521f1371e1c7f11a6dfa712b1f05c87`; the local
prototype tree is `6bc9d9e5519688749cd37f58dca937f77bd9ba45`. Patch SHA-256:
`95eb05b20b91886f9b889a06cdc51742b25d6a290ee331b86d40e06fa8d9c6ae`.

Untimed checks retained 26 inode lookups, 52 attribute write-lock entries
and 26 wrapper read-lock entries. Paired RCU entries in the removal functions
and wrappers fell from 234 to 78. That count reduction is not a proportional
latency prediction. The separate clean run found only about 4–5% improvement;
see the `root-reuse-r1` rows in [bare-metal data](../bare-metal/README.md).

Notification delivery, open-FD link-count clearing, namespace aliases on the
same superblock and bounded concurrent access/removal were tested. True
multi-superblock cases and runtime lockdep were not covered. Lockdep-enabled
object compilation is not a runtime correctness test. Creation showed small
changes of −0.265% to +3.150%; a secondary deletion ratio had CV 3.050% and
was not interpreted. This is not a complete fix or a submitted patch, and
its benefit has not been tested on the pinned 7.3-rc2 kernel.

The separate inode-return probe confirmed many misses, but no simple safe
skip condition was established. Lack of an open FD or watch, a low reference
count, or a previous lookup miss is insufficient to rule out an existing or
concurrently instantiated inode. A stronger design would need correct state
publication, lifetime and multi-superblock synchronization; it was not implemented.
