# Unsent Linux performance-regression candidates

This directory groups curated evidence that has not been submitted upstream.
Placement here means only "not sent"; it does not imply that a report has been
approved, and it does not change the scope documented by each bundle.

| Bundle | Current disposition | Main boundary |
| --- | --- | --- |
| [`io-uring-futex-waitv-accounted-allocation/`](io-uring-futex-waitv-accounted-allocation/) | Clearest current send candidate | Preserve memcg-accounting semantics and ask only whether per-WAITV allocation cost can be reduced. |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | Reportable | Covers guaranteed-miss async cancel only; attribution stops at the whole commit. |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | Prepared; refresh the upstream audit before sending | Covers repeated setup and teardown of short-lived standard rings only. |
| [`io-uring-nop-diagnostic-control/`](io-uring-nop-diagnostic-control/) | Closed; no report recommended | NOP is a test/control opcode and does not represent application I/O. |

Before sending any candidate, refresh the upstream duplicate/fix audit, rerun
`get_maintainer.pl`, and check the public file boundary. Local mail drafts stay
excluded by `.gitignore`.
