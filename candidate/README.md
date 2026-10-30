# Linux performance and optimization candidates

This directory groups four performance and optimization-discussion candidates.
Each bundle describes its measurements, mechanism and remaining limitations.

| Bundle | Current disposition | Main boundary |
| --- | --- | --- |
| [`mm-swap-count-batch-cleanup/`](mm-swap-count-batch-cleanup/) | Locked-batch cleanup prototype | Fixed MM-tree pair saves about 0.927 us per 16-page munmap in the retained-table condition; position-dependent synthetic result, no established application gain. |
| [`mm-lazyfree-dirty-pte-batch-354dffd2/`](mm-lazyfree-dirty-pte-batch-354dffd2/) | Local optimization evidence; whole-task benefit unestablished | About 61 us saved in the PTE failure path; lifecycle effects are similar to variation and natural application hits are unestablished. |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | Reportable | Covers guaranteed-miss async cancel only; attribution stops at the whole commit. |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | Measured lifecycle cost increase | Covers repeated setup and teardown of short-lived standard rings only. |
