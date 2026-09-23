# Unsent Linux performance-regression candidates and closed diagnostic

This directory groups two curated report candidates that have not been
submitted upstream, plus one unsent NOP diagnostic that is already closed.
Placement here does not imply approval to report and does not form an active
experiment queue.

| Bundle | Current disposition | Main boundary |
| --- | --- | --- |
| [`io-uring-async-cancel-miss-5623eb1e/`](io-uring-async-cancel-miss-5623eb1e/) | Reportable | Covers guaranteed-miss async cancel only; attribution stops at the whole commit. |
| [`io-uring-region-api-ring-lifecycle/`](io-uring-region-api-ring-lifecycle/) | Prepared; refresh the upstream audit before sending | Covers repeated setup and teardown of short-lived standard rings only. |
| [`io-uring-nop-diagnostic-control/`](io-uring-nop-diagnostic-control/) | Closed; no report recommended | NOP is a test/control opcode and does not represent application I/O. |

## Existing-patch validation

[RT sysctl reads](sched-rt-sysctl-read-rebuild/) records validation of Joseph
Salisbury's existing proposal, not a new fix. The v7.2 read-loop cost falls by
96.9%; successful writes retain their maintenance. Feedback is not yet sent,
and the proposal is not present in the inspected mainline revision.

Before sending any candidate, refresh the upstream duplicate/fix audit, rerun
`get_maintainer.pl`, and check the public file boundary. Local mail drafts stay
excluded by `.gitignore`.
