# Linux performance and optimization candidates

This directory contains seven standalone performance and optimization-discussion
bundles and an [additional review inventory](additional-review/README.md).
Its 22 entries distinguish v7.2 evidence, older results awaiting validation and
remaining limitations; they are not 22 established independent bugs.
Existing bundles and already reported work are not duplicated.

The [sembench new-idle report](../kernel/sched/sched-newidle-sembench-tradeoff/)
moved to the scheduler section after the user confirmed sending it on November 5.

| Bundle | Current disposition | Main boundary |
| --- | --- | --- |
| [`io-uring-mixed-cqe-nop-e26dca67/`](io-uring-mixed-cqe-nop-e26dca67/) | Exact shared-completion attribution and bilingual draft complete; not sent | Native MMTests 20-thread ordinary NOP throughput falls 5.564% across mixed-CQE support, with −0.0324% parent drift. Not an attribution of the entire roughly 20% version gap or a storage-performance result. |
| [`mm-madv-cold-deactivation-tradeoff/`](mm-madv-cold-deactivation-tradeoff/) | Exact-commit, state-dependent tradeoff | Per 16 MiB, fresh-file CPU +51 µs / +13.20%; oldest-file CPU −247 µs / −68.30%. Not a whole-application regression or a recommendation to revert the optimization. |
| [`mm-zswap-limit-rstat-flush/`](mm-zswap-limit-rstat-flush/) | Attribution and counter evaluation for a known issue | Exact commit: single-worker program CPU +15.56%. Separate MM counter pair: −21.78%; no clear single-worker unlimited cost detected. Concurrent tradeoffs and production impact remain open. |
| [`mm-swap-count-batch-cleanup/`](mm-swap-count-batch-cleanup/) | Locked-batch cleanup prototype | Fixed MM-tree pair saves about 0.927 us per 16-page munmap in the retained-table condition; position-dependent synthetic result, no established application gain. |
| [`mm-lazyfree-dirty-pte-batch-354dffd2/`](mm-lazyfree-dirty-pte-batch-354dffd2/) | Local optimization evidence; whole-task benefit unestablished | About 61 us saved in the PTE failure path; lifecycle effects are similar to variation and natural application hits are unestablished. |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | Reportable | Covers guaranteed-miss async cancel only; attribution stops at the whole commit. |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | Measured lifecycle cost increase | Covers repeated setup and teardown of short-lived standard rings only. |
