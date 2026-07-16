# Btrfs Writeback-Inhibition Inline-Buffer v2 Validation

This directory contains an independent bare-metal validation of:

```text
[PATCH v2] btrfs: replace writeback inhibition xarray with a fixed inline buffer
```

Upstream thread:

```text
Message-ID: <12d3c3f07b8610ca13b0f3f792d420541afb7b33.1782949130.git.loemra.dev@gmail.com>
https://lore.kernel.org/linux-btrfs/12d3c3f07b8610ca13b0f3f792d420541afb7b33.1782949130.git.loemra.dev%40gmail.com/
```

The v2 code diff was applied cleanly to a frozen Linux 7.1.0 source snapshot
containing the Btrfs state introduced by:

```text
f9a48549a15aa369d42cebc08a6a72b71a53d547 btrfs: inhibit extent buffer writeback to prevent COW amplification
```

The exact saved diff used for the build has SHA-256:

```text
5ec741be5a89d6dae0c0608cc036512770b55d6a49e9b576b4aa3115ebfdffd3
```

## Result

The primary result is a matched `control -> v2 -> control` sandwich.  Values
below are all-round means in ns/op; negative deltas mean that v2 was faster.

| operation | control A | upstream v2 | control B | control midpoint | v2 vs midpoint |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 KiB `FICLONERANGE` | 2943.790 | 2159.123 | 2970.446 | 2957.118 | **-26.986%** |
| 4 KiB `FIDEDUPERANGE` | 3535.456 | 2762.835 | 3546.942 | 3541.199 | **-21.980%** |

The two outer control points drifted by only `+0.905%` for clone and `+0.325%`
for dedupe.  Dropping the first round from each point produced essentially the
same result: `-27.178%` and `-22.128%` respectively.

All 90 timing rows in this three-point sandwich reported
`expected_match_ratio=100` and `unexpected_results=0`.  A separate direct-hit
run also observed all required functions:

| function | calls |
| --- | ---: |
| `btrfs_remap_file_range` | 2,000 |
| `btrfs_inhibit_eb_writeback` | 7,473 |
| `btrfs_uninhibit_all_eb_writeback` | 2,033 |

## Test Shape

- physical x86-64 machine: Intel Core i7-12700KF, 32 GiB RAM;
- Btrfs created fresh on a 1 GiB `/dev/ram0` brd device for each point;
- one P-core logical CPU (`CPU 2`), `intel_pstate`, `performance` governor;
- `CONFIG_PREEMPT_DYNAMIC=y` with boot argument `preempt=none`;
- 15 external rounds per kernel point;
- 10,000 4 KiB clone operations and 10,000 4 KiB dedupe operations per round;
- the same frozen Linux 7.1.0 base source snapshot, normalized config, GCC
  15.2.0 toolchain, and reproducible Kbuild identity for control and v2.

## Scope and Caveats

This is a source-calibrated synthetic syscall micro-workload.  It is not a
production-application benchmark, a physical-storage test, or a claim about
all Btrfs or generic `remap_range` workloads.  The result supports the v2 patch
for this specific Btrfs 4 KiB clone/dedupe shape; it is not a substitute for
the patch author's correctness tests or broader filesystem testing.

The upstream report and v2 patch predate this validation.  This bundle is
supporting test evidence, not a first-discovery or authorship claim, and it
does not publish a competing local implementation.

## Contents

- `reproducer/`: standalone C workload and a guarded brd/Btrfs runner;
- `bare-metal/`: compact timing, build-identity, semantic, and direct-hit
  evidence from the matched bare-metal run;
