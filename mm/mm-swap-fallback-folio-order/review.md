# Reclaim-local ordering review

Reviewed against **mm-stable 2967f3ccdb5fae956f1b085370ec292bab5c7e25**,
2026-10-15. The measured diff is unchanged.

| Boundary | Source-based reasoning | Evidence and remaining limit |
| --- | --- | --- |
| Entry | Only the `-E2BIG` swap-allocation fallback in `shrink_folio_list()` uses the new helper. The preceding partially-mapped split remains unchanged. | Exact source restoration and call-site checks pass. |
| Object scope | Anonymous, non-swapcache, PMD-order folios only. Other states call the original helper, including THP-disabled builds. | 4,480 local list cases across THP on/off; actual x86 build and admitted swap fallback. |
| Child lifetime | `lru_add_split_folio()` takes a reference before putting each child on the supplied list. Children are not on the shared LRU. The subsequent unlock/put leaves the reclaim-list reference in place. | Source review of `__folio_freeze_split_anon()` and `__folio_split()`; no refcount or locking changes in the patch. Not a comprehensive race proof. |
| Existing queue | The temporary list contains only new children. Reverse-tail moves append these children; existing queue entries and the original head keep their relative positions. | Nonempty-queue and 0–511-child local tests; actual ordering observations. |
| Failure | The helper returns the same error and uses `list_splice_tail_init()` to preserve the old append order of any children. The caller still activates/keeps the original folio and later drains its list. | Error and partial-output cases are local-model checks, **not** kernel fault injection. |
| Failure reachability | This selected anonymous uniform split passes `xas = NULL` to `__split_frozen_folio()`. Its explicit partial-split `-ENOMEM` return is in non-uniform XArray splitting, not this path. Unexpected references/anon-VMA failures can stop the selected path before splitting. | This narrows the claim; it does not make all generic partial-split failures irrelevant forever. Defensive splice handling remains. |
| Other callers | `mm/huge_memory.c`, migration, shmem and other `split_folio_to_list()` callers are untouched. | Checked source identities and exact one-call replacement. No claimed testing of every caller. |
| Added cost | One pass over successful PMD children; no extra allocation, reference operation or synchronization primitive. | Included in the measured pair; other machines/backends may have different tradeoffs. |

Relevant fixed source:
[reclaim caller](https://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git/tree/mm/vmscan.c?id=2967f3ccdb5fae956f1b085370ec292bab5c7e25),
[split implementation](https://git.kernel.org/pub/scm/linux/kernel/git/akpm/mm.git/tree/mm/huge_memory.c?id=2967f3ccdb5fae956f1b085370ec292bab5c7e25).

No new blocking correctness issue was found in this bounded review. Five
wrong variants (wrong order, dropping failed children, and missing guards)
are detected by the local tests. Those tests do not justify claiming
allocation-failure injection, arbitrary concurrent remapping, all
architectures, or all reclaim contexts have passed.

The next useful check is maintainer review of ordering policy and scope.
This review does not require another identical performance matrix. If a
specific concurrent-state counterexample is identified, reproduce that
case before revising the claim or patch.
