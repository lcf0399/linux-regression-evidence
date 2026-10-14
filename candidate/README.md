# Unsent Linux performance and optimization candidates

This directory groups four curated report or optimization-discussion candidates that have not been
submitted upstream.
Placement here does not imply approval to report and does not form an active
experiment queue.

| Bundle | Current disposition | Main boundary |
| --- | --- | --- |
| [`mm-lazyfree-dirty-pte-batch-354dffd2/`](mm-lazyfree-dirty-pte-batch-354dffd2/) | Retained optimization-discussion candidate; not sent | About 61 us saved in the PTE failure path; lifecycle effects are similar to variation and natural application hits are unestablished. |
| [`sched-fair-delayed-requeue-handoff-333238da/`](sched-fair-delayed-requeue-handoff-333238da/) | Technical-discussion candidate; not submitted upstream | Same-CPU light-computation handoff; the revert is diagnostic, not a safe fix. |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | Reportable | Covers guaranteed-miss async cancel only; attribution stops at the whole commit. |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | Prepared; refresh the upstream audit before sending | Covers repeated setup and teardown of short-lived standard rings only. |

Before sending any candidate, refresh the upstream duplicate/fix audit, rerun
`get_maintainer.pl`, and check the public file boundary. Local mail drafts stay
excluded by `.gitignore`.
