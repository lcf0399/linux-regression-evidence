# Status as of 2026-09-21

The candidate is based on the observed effect of reverting
`333238da9a193ffc58792995f3e951e4cb87bfd2` from v7.2. The original change corrects
accounting before delayed requeue; the revert is diagnostic only.

The latest kernel measured for this case was unmodified v7.3-rc4,
`93f51579e7df248780214094418f205253383cc5`; it did not materially improve the
earlier handoff-only case. This is a dated measurement, not an assertion that
no later or equivalent fix exists.
