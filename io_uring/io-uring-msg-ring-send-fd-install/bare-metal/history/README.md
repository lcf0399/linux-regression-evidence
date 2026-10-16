# Earlier cache and allocator diagnostics

These results remain useful for understanding the measured installation cost.
They are historical, separately controlled experiments, not extra measurements
of the current shared-128 prototype. Their distinct numeric evidence is kept;
the repeated step-by-step narrative has been removed from the main pages.

| Question | Result | Preserved evidence |
| --- | --- | --- |
| Can existing nodes be reused? | At v7.1.3, 128-slot cold/warm installation changed from 137.789 to 109.643 ns/install. The cold control drift was −2.498%, so this is diagnostic, not a new formal regression estimate. | [Timing](node-cache-cold-warm.tsv), [counts](node-cache-trace.tsv) |
| Why was the first dedicated-slab patch slower? | Cold first fill was about 8% slower. Disabling slab merging did not resolve it; a separate matched removal of the unintended `SLAB_ACCOUNT` flag removed the added cost. It does not justify disabling required accounting generally. | [Accounting follow-up](uzair-patch1-followup.tsv) |
| Did the corrected dedicated slab or batch 32 help first fill? | v6.18-rc4 six-boot comparison: unpatched 119.188, patch 1 118.407, patches 1+2 118.415 ns/install. Incremental batch benefit was effectively zero; independent counts confirmed execution. | [Timing](uzair-v2-bulk-refill.tsv), [counts](uzair-v2-bulk-refill-mechanism.tsv) |
| Does new slab-page creation explain the first-fill gap? | Priming backing pages removed 32 nested page-creation calls but measured +0.927%; no benefit. Full raw-node prefill moved allocation/zeroing out of the interval and reduced first fill by 9.493%, without measuring its registration or retained-memory cost. | [Allocation diagnostics](first-fill-allocation-diagnostics.tsv) |

The first-fill diagnostics apply to `7029acd8a950`. Their exact
[full-prefill patch](../../reproducer/0001-diagnostic-prefill-sparse-node-cache.patch)
and [slab-backing patch](../../reproducer/0002-diagnostic-prime-node-slab-backing.patch)
remain available. They are not fix proposals and do not demonstrate a lower
total cost. Allocation, initialization and locality effects were not fully
separated.

The later [v3 experiment](../uzair-v3-prefill/README.md) measured registration
cost and retained memory explicitly. The [current batch-32 follow-up](../dedicated-slab32/README.md)
adds small batches and TCP; the [shared-128 experiment](../shared-slab128/README.md)
is a different implementation. Do not combine their absolute timings or
subtract percentages across baselines.
