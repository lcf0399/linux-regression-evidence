# F01: batch shared swap-count cleanup checks

Unsent optimization-discussion candidate, separate from the F02 folio-order
prototype. The fixed baseline is MM snapshot
`2967f3ccdb5fae956f1b085370ec292bab5c7e25` (7.3-rc5), already including
Kemeng Shi's early-return scan. The second kernel adds only this batching
prototype, not F02.

An unmap batch can repeatedly inspect the same extended count table under one
cluster lock. The prototype accumulates the need for cleanup and checks before
freeing a pending slot batch and before unlocking. It preserves the 15->14 and
14->13 thresholds; other callers still check immediately.

Baseline A -> prototype -> baseline B averaged **3.307 -> 2.337 -> 3.222 us**
per 16-page munmap in the high-count condition. Against the combined baseline
mean of 3.264 us, the saving is **0.927 us (28.39%)**. Baseline drift was
-2.56%; point CVs were 14.04%, 4.93%, and 16.55%. Slot-position sequences
matched, but distributions overlap. This is an average for these positions,
not a universal percentage. The eight-holder control had no clear change.

The synthetic test retains one high-count page while three children each unmap
16 pages. It isolates repeated cleanup work; common application impact has
not been established. Separate bounded checks cover last-high-count release,
mixed transitions and concurrent count changes, not all configurations or races.

- [Tested patch](prototype/batch-cleanup.patch)
- [Standalone reproducer](reproducer/README.md)
- [All compact timing rows, builds, and offline verification](bare-metal/README.md)
- [Review scope and separate historical results](review.md)
- [Chinese explanation](README.zh-CN.md)

This proposal has not been submitted upstream. DCO confirmation and a fresh
upstream/recipient check remain pre-send tasks. Publication adds no new
performance measurements or maintainer endorsement.
