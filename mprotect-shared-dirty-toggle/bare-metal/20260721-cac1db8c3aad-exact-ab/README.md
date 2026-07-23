# Exact A/B for `cac1db8c3aad`

This directory records a bare-metal direct-parent/child test of:

```text
cac1db8c3aad ("mm: optimize mprotect() by PTE batching")
```

## Result

The commit increases `iteration_ns_per_page` by **39.77%** relative to the
midpoint of its two surrounding parent controls. Lower is better.

| Point | Commit | n | Mean ns/page | SD | CV | Values |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| parent A | `45199f715b74` | 15 | 38.133 | 0.743 | 1.95% | 37 38 38 38 39 37 39 37 38 39 38 38 39 39 38 |
| child | `cac1db8c3aad` | 15 | 53.533 | 0.516 | 0.96% | 53 53 54 53 53 54 54 54 53 54 53 53 54 54 54 |
| parent B | `45199f715b74` | 15 | 38.467 | 0.640 | 1.66% | 37 39 38 39 38 39 38 39 38 39 39 39 38 39 38 |

The parent midpoint is `38.300 ns/page`. Parent B differs from parent A by
only `+0.87%`. Dropping the first measured process from every point leaves a
`+39.53%` child delta and `+0.93%` parent drift.

All 45 measured processes passed the semantic and state checks:

```text
expected_match_ratio=100
unexpected_results=0
KernelPageSize=4 KiB
MMUPageSize=4 KiB
AnonHugePages=0
```

The component measurements move in the same direction. The mean protect and
restore costs are both `13 ns/page` in the parent controls and `20 ns/page` in
the child; post-touch remains close to `12 ns/page`. The total iteration
metric above is the primary result because the component values are rounded
to integer ns/page.

For this narrow workload, the direct parent/child result identifies
`cac1db8c3aad` as the source of the measured slowdown. It does not establish a
generic `mprotect()` or application-level regression.

The absolute values should not be compared directly with the older i7-14700
release-window runs. The hardware and kernel build baseline differ; commit
attribution comes from the matched three-boot sandwich in this directory.

## Exact source and build contract

- parent: `45199f715b7455a2e4054dbc5dab0c3b65e2abc1`
- child: `cac1db8c3aad97d6ffb56ced8868d6cbbbd2bfbe`
- `45199f715b74` is the direct parent of `cac1db8c3aad`
- the commit changes only `mm/mprotect.c` (`+113/-12`)
- both exact source trees report kernel version `6.16.0-rc5`
- sources were downloaded by exact commit ID; archive and source hashes are
  in `source-manifests/`
- after removing only `CONFIG_LOCALVERSION`, the two configs are byte-for-byte
  identical; canonical SHA-256:
  `66064617009cb0b8d49edd1cd2fd1c7876965a3c1d0ec7304f0d54e0ce40c171`
- both kernels used GCC 15.2.0 and the same Kbuild timestamp, user, host,
  build version, and external module-signing key
- the private signing key and kernel source/build trees are not published

## Workload and run order

The workload is the existing standalone reproducer in `../../reproducer/`:

- 64 MiB `MAP_SHARED | MAP_ANONYMOUS` mapping
- prefault and write-dirty all 4 KiB pages
- repeatedly protect the full mapping read-only, restore write permission,
  and write-touch every page
- 1,000 iterations per measured process, with 10 internal warm-up iterations
- 3 external warm-up processes and 15 measured processes per kernel point

The test ran on an Intel Core i7-12700KF system with 32 GiB RAM. Each point
used a fresh boot, CPU 2, the `performance` governor and EPP, Turbo disabled,
and runtime `preempt=none`:

```text
parent A -> child -> parent B
```

All three boots had distinct boot IDs, passed the expected-kernel smoke check,
and reported zero failed systemd units. The runner restored the original CPU
governor, EPP, and Turbo state after each point. The machine was returned to
its distribution rescue kernel after the sandwich.

## Files

- `summary.tsv`: primary three-point result.
- `sensitivity.tsv`: parent-midpoint and drop-first-round checks.
- `component-summary.tsv`: protect, restore, and post-touch means.
- `runs/`: measured rows, raw reproducer output, CPU profiles, and run
  environment for the three points.
- `source-manifests/`, `build-metadata/`, `install-metadata/`: exact source,
  config, artifact, and installation hashes.
- `prepare_build_install_exact_pair.sh`: guarded exact-pair build/install
  script.
- `run_exact_ab_point.sh`: guarded single-point runner.
