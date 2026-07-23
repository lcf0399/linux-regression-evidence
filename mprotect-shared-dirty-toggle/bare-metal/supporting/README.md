# Supporting bare-metal evidence

These runs retain independent supporting value but do not carry the final
commit-level attribution.

| Directory | Role | Primary result |
| --- | --- | --- |
| `20260623-narrow-6.16-6.19-3rounds/` | release-window narrowing | `v6.16=25.000` and `v6.17=37.000 ns/page`; the slowdown first appears in `v6.17` |
| `20260630-single-protect-followup/` | checks that the signal is not specific to repeated toggling | one protect measured `v6.16=8.000` and `v6.17=14.000 ns/page` |
| `20260706-folio-order-check/` | page-state semantic gate | all nine runs used 4 KiB order-0 base pages with no compound/THP backing |

The current culprit and mechanism conclusions are in
`../20260721-cac1db8c3aad-exact-ab/` and
`../20260722-cac1-folio-batch-decomposition-ab/`.
