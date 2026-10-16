# Upstream status audit

Latest status check: 2026-10-10. The new TCP and patch results were sent to Uzair
on October 1 (UTC+08:00). Gmail, including Spam/Trash, contains no later reply.
The public RFC series still ends with Gabriel's September 25 UTC reply
(`87cxu1yv61.fsf@mailhost.krisman.be`, September 26 in UTC+08:00). No separate
patch-2 review was located. The checked mainline `3857c2fe5449` (v7.3-rc6) still
uses the fixed resource-node cache and individual allocation, not the proposed
dedicated-slab/bulk/prefill changes. No acceptance or merge is claimed; this is
not a new benchmark of that mainline build. Private correspondence stays private.

The remaining text is the historical 2026-08-07 thread/source snapshot.

The introducing change was posted by Jens Axboe as
[`[PATCH 06/14] io_uring/rsrc: get rid of per-ring io_rsrc_node list`](https://lore.kernel.org/io-uring/20241029152249.667290-7-axboe@kernel.dk/)
and merged as `7029acd8a950`. The patch deliberately moved resource-node
allocation to registration time to remove per-ring serialization and avoid
resource reclamation stalls. The evidence in this bundle measures that narrow
registration/update trade-off and does not suggest reverting the change.

A later patch, merged as
[`ed9f3112a8a8`](https://github.com/torvalds/linux/commit/ed9f3112a8a8f6e6919d3b9da2651fa302df7be3),
restored caching for resource nodes and mapped buffers because frequent
allocation/free cycles were expensive. That commit is already contained in
Linux 7.1 and 7.1.3. The matched Linux 6.12.95/7.1.3 check still showed a
`15.602%` F0 difference, so the later cache does not remove the measured
release signal for this workload.

An updated source audit at Linus master `fcaeecb8b0cd` on 2026-08-06 found
that `io_msg_install_complete()` still reaches `io_rsrc_node_alloc()` through
`__io_fixed_fd_install()`, with the node cache in place. The master source was
not benchmarked, so this is a source-topology statement only. Targeted searches
of the public io-uring archive by the introducing commit, opcode, and allocation
path did not surface an existing report for the same MSG_RING SEND_FD bulk
empty-slot installation workload.

A new narrow regression report linking the original patch 06 thread was sent
on 2026-08-07 at 16:16 UTC. Gmail confirms the exact sender and recipients,
and the message is publicly archived by the io-uring lore archive. No bounce,
list hold, or maintainer reply had been observed at the time of this update.
Exact message IDs and source references are in [`refs.tsv`](refs.tsv).
