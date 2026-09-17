# Separate inode-request marker: prototype results

Measured September 16, 2026; published September 17. This is a new prototype,
not the earlier [INODE_INITED patch](../inode-inited/README.md).
In one four-boot comparison, original-test `rmdir()` latency fell by
22.36–22.72%, and continuous `mkdir → first directory stat → rmdir` latency
fell by 7.35–9.01%. The cost is 8 extra bytes per `kernfs_node` in this build.
Correctness testing is bounded; this is not a finished fix or a measured
application-level improvement.

## Exact comparison and method

- Base: `2f0c1cf72f4682178506f513bbf015e591b1aa4a` (7.3-rc2).
- A: base plus [Shakeel's file-handle read-lock fix](patches/shakeel-read-lock.patch).
- B: A plus the [tested inode-requested prototype](patches/inode-requested.patch),
  SHA-256 `15d088b15e4bf290cd894493b6b5a9ea7f5b0f33f9d49b642daf49aa838fae47`.
  The old INODE_INITED changes are absent.
- Fresh boots: A1 → B1 → B2 → A2; one matrix, not two independent matrices.
  Bare-metal i7-12700KF, CPU0, sched_ext disabled, full preemption,
  performance governor, Turbo off. GCC 15.2; configs match except LOCALVERSION.
- Nine samples per case per boot, 16 warmups per sample. The unchanged
  original binary measures 128 operations; the continuous-sequence and
  lookup binaries each measure 32. Cases rotate order between sample rounds.
  There is a 45-second boot settling period and a 2-second wait between
  invocations, outside timing; no waits between measured operations.
- Empty cgroup v2 leaves, no tasks or watches. Original and sequence cases
  process 26 kernfs nodes. The sequence is directly timed, not the sum of
  separately computed medians. It excludes parent setup and completion of
  deferred reclamation. The lookup case holds one control-file FD.
- These are clean timings. Tracing and correctness checks run separately.
  All measured tails are retained. Stability limits: population CV ≤3%,
  repeated-arm boot drift ≤2%; a ≥5% effect must also survive drop-first.
  Percentage ranges compare all four B/A boot pairs, not confidence intervals.

The prerequisite is exported as a source-only diff; its public-file hash is
in `provenance.json`. `identity.json` separately records the original patch
input's hash. The prototype diff itself is byte-identical to the tested input.

## Results

Medians of nine sample means, µs unless indicated. Negative B/A change means
lower latency. [All sample means](measured-rounds.tsv) and the
[full statistics](summary.json) retain every metric and comparison.

| Metric | A1 | B1 | B2 | A2 | Maximum CV | Interpretation |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Original mkdir | 18.399 | 18.983 | 18.984 | 18.702 | 7.35% | Unstable |
| Original rmdir | 9.758 | 7.541 | 7.563 | 9.741 | 1.50% | 22.36–22.72% lower |
| Sequence mkdir | 18.290 | 17.956 | 18.002 | 17.957 | 2.95% | No stable ≥5% effect |
| First directory stat | 1.782 | 1.776 | 1.771 | 1.767 | 2.85% | No stable ≥5% effect |
| Sequence rmdir component | 9.864 | 7.539 | 7.637 | 9.766 | 4.69% | Unstable component |
| Whole continuous sequence | 30.023 | 27.317 | 27.356 | 29.526 | 1.95% | 7.35–9.01% lower |
| First file stat | 1.693 | 1.682 | 1.638 | 1.698 | 24.52% | Unstable; B drift −2.57% |
| Cached file stat batch, ns/call | 809.170 | 805.379 | 802.853 | 809.625 | 0.31% | No stable ≥5% effect |

For original rmdir, A/B boot drift is −0.177%/+0.288%; for the whole
sequence it is −1.656%/+0.142%. After dropping each boot's first sample,
reductions remain 22.36–22.68% and 7.40–8.53%, respectively.
The noisy sequence rmdir component does not invalidate the separately
measured whole-sequence distribution, or make that component stable.
Cached-file timing includes the 1,024-call loop and checks, not just syscalls.
First-file timing cannot establish the cost of the added marker write.

## Why it helps, and what it costs

The patch sets a separate `inode_ever_requested` boolean before `iget_locked()`
and never clears it, including after allocation failure or inode eviction.
A false marker skips removal's inode lookup; a true marker retains the
existing per-superblock lookup and link-count handling. It does not write
the shared `kn->flags` field or add an initialization lock.

Separate traces cover 40 operation windows, following six semantic smokes:

| Removal case | ilookup, A → B | clear_nlink, A → B |
| --- | ---: | ---: |
| Original and continuous-sequence cases | 26 → 1 | 1 → 1 |
| One file FD held open | 26 → 2 | 2 → 2 |

Deletion write-lock counts (52) and wrapper read-lock counts (26) are unchanged.
Initial lookup still allocates/initializes one inode; cached lookup does not.
There is no new write-lock acquisition on first lookup. See [mechanism counts](mechanism.json).

Creation/removal still rely on existing lifetime synchronization;
`READ_ONCE`/`WRITE_ONCE` do not supply that synchronization. The source review
found five `kernfs_get_inode()` callers: four use `kernfs_rwsem` with the
prerequisite applied; `cgroup_may_write()` relies on `cgroup_mutex`. These
assumptions still need review. Unlike a watcher-count shortcut, this test
does not treat “no watchers” as proof that no live inode needs `nlink` cleared.

[Build layouts](layout/) and runtime slab records agree: 136 → 144 bytes,
30 → 28 objects per order-0, 4 KiB slab, alignment 8. The boolean adds one
byte and seven alignment bytes. Merely moving the field does not avoid the
increase in this build. This is an object/slab cost, not a whole-machine
memory measurement. No safe size-neutral alternative has been established.

## Bounded correctness checks and remaining gaps

[Recorded checks](semantics.json), on both A and B:

- 16 ordinary removals, eight held-FD link-count checks, eight watch-only
  positive controls, 16 close-before-removal path/handle controls, and one
  namespace-alias test with two watch instances on the same superblock.
- 192 bounded path/handle open-remove rounds, 768 open attempts per kernel.
  Successful FDs reported `nlink=0`. The path-race group only returned ENOENT;
  watches/handles had already accessed targets, so these checks did not
  establish untouched-file first-creation coverage.
- A separate fresh-file test added 176 untraced and 176 traced cases per
  kernel: 704 total, including ordered controls. Each kernel had 80 successful
  opens untraced and 96 traced; all remaining opens returned ENOENT, all
  removals succeeded, and successful FDs reported `nlink=0` afterward.
  Tracing confirmed 96 new inodes per kernel: 16 ordered controls and 80 races.
  In B each marker changed from false at entry to true before `iget_locked()`.
- Each kernel had 32 first creations after rmdir entry and one rmdir entry
  during the target's get-inode interval. **Neither captured the same rwsem's
  deletion write lock waiting across first creation.** These are different
  observations; a syscall overlap is not proof of the missing lock window.

No kernel warning, taint or trace loss was recorded. Tracing changes race
outcomes; its success rates are not general probabilities or timing evidence.
Held-FD notification deadlines were removed from the corrected test because
closing an FD need not immediately evict its dentry. Valid notification
positive controls remain. Earlier diagnostic failures are not evidence of
a Shakeel-patch regression. First-creation probe attempts r1/r2 failed in the
observer; r3 passed, without changing the helper, patch or timing results.

Still untested: proven inode eviction/recreation, distinct superblocks,
other kernfs users, weak-memory architectures, KCSAN/lockdep and all relevant
interleavings. These results do not prove full concurrency safety.

## Files and offline verification

Run `python3 -B verify.py` here. It checks exported hashes and recomputes all
eight metrics, CVs, drift and drop-first from 288 metric rows representing
108 invocations. It also checks recorded bounded-test counts, not kernel safety.
The local export replayed the raw timing/trace/semantic records before
publication; raw traces, private correspondence and kernel binaries are not included.

- [Identity](identity.json), [provenance](provenance.json), [compact table](result-summary.tsv).
- [Reproduction instructions and auxiliary sources](reproducer/README.md).
- [中文说明](README.zh-CN.md).
