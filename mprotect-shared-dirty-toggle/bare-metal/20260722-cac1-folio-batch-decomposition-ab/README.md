# `cac1db8c3aad` folio-lookup and commit-path decomposition

This experiment uses three diagnostic probes on the exact
`cac1db8c3aad` child to separate three costs:

1. the generic single-PTE commit/write/flush path;
2. normal-path batch discovery;
3. the normal-path `vm_normal_folio()` lookup.

The nine fresh-boot points were:

```text
parent A -> child A -> fastpath A -> folioonly A -> nofolio ->
folioonly B -> fastpath B -> child B -> parent B
```

| Role | midpoint `ns/page` |
| --- | ---: |
| parent | 38.800 |
| child | 52.967 |
| fastpath | 46.867 |
| folioonly | 46.900 |
| nofolio | 40.600 |

Relative to the absolute parent-to-child gap, the single-PTE commit path
explains `43.06%`, batch discovery `-0.24%`, and folio lookup `44.47%`.
Together they explain `87.29%`. The drop-first analysis has the same direction
and magnitude. All 135 measured processes passed the return-value, 4 KiB
page-state, and no-THP gates.

The diagnostic probes bypass some generic semantics. They are attribution tools, not
proposed fixes. `experiment-plan.zh-CN.md` records the complete order and
preregistered decision rules. The aggregate results are in `summary.tsv`,
`component-summary.tsv`, `sensitivity.tsv`, and `decision.tsv`.

The three diagnostic patches used for `fastpath`, `folioonly`, and `nofolio`
are included in this directory. `prepare_build_install_folio_batch_probes.sh`
can rebuild all three from the same exact child source.
