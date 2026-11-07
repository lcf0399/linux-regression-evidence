# Direct-commit CPU-profile summary

`commit-cpu-profiles.csv` contains 729 curated symbol rows for the direct
`96cce16e26dd` parent / `a3af84b0fa00` child / parent comparison.
The same i7-12700KF and normal 0x3e microcode were used, without an extra
protection trigger. Each boot had one unprofiled call and two CPU-clock
profiles at 1999 Hz; all three points recovered generic after collection.

For each command, role, address space, symbol and DSO basename:

- `parent_first`, `child`, `parent_return` are sample counts averaged over
  the two profiles in that boot. Each profile sums five native windows
  for the command; counts can therefore end in 0.5.
- `baseline` is the mean of the two parent columns.
- `delta_samples` is child minus baseline; `return_delta_samples` is the
  second parent minus the first. Negative differences and unknown symbols
  are retained, not silently removed.

Only ZPOPMIN and the GET control are exported here. These are sampled
execution locations, not function-call counts, exclusive function timings
or the clean throughput measurements. Raw profiles, process-level details
and correspondence stay local. See the [case overview](../README.md).
