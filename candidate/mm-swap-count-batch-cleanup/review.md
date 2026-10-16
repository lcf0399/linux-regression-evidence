# Review scope

The RFC asks whether deferring repeated extension-table checks within one
locked batch, but completing them before slot release and unlock, is a useful
and acceptable tradeoff. It builds on the existing early-return optimization;
it does not replace that work or claim the historical slowdown is fully fixed.

The current fixed-MM pair passed separate state/entry and release/concurrency
checks before clean timing. The held-lock count observations are summarized in
[boundary-observations.tsv](bare-metal/boundary-observations.tsv). Each row is
one probed condition; the full boundary set also contained an unprobed run per
condition, giving six runs per kernel and twelve total. Counts are observations,
not elapsed-time measurements or exhaustive race coverage. Inlined scan loops
were not directly branch-traced.

Historical results are not pooled into the current pair:

- On v7.2 plus the author's early-return patch, batching reduced the retained-
  table mean from 3.287 to 2.425 us/16-page munmap (26.22%); drift was 5.50%.
- A separate v7.2 lifecycle test retained no high-count anchor. Its first full
  17-page unmap dropped from 4.744 to 3.091 us, but whole-test mean changed only
  -1.36%, smaller than its 2.36-3.52% CV. The release-stage mean changed -3.63%,
  similar to -3.65% return drift. These are not current-MM or application gains.

The patch, test, analysis and RFC used Codex assistance. Drafts retain that
disclosure. No maintainer endorsement or DCO signature has been inferred from
another patch. Upstream state must be refreshed before submission; curation
did not perform a new upstream audit.
