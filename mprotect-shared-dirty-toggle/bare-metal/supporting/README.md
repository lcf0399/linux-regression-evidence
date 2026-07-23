# Supporting bare-metal results

These compact tables retain results that support, but do not replace, the
exact commit-level attribution:

| File | Role | Primary result |
| --- | --- | --- |
| `release-window-summary.csv` | release-window narrowing | `v6.16=25.000`, `v6.17=37.000 ns/page`; the slowdown first appears in v6.17 |
| `single-protect-summary.csv` | checks a single protection change | `v6.16=8.000`, `v6.17=14.000 ns/page` |
| `folio-order-summary.csv` | base-page state gate | all nine rows used 4 KiB order-0 pages with no compound/THP backing |

The detailed historical runners and intermediate logs are retained only in
the local experiment archive.
