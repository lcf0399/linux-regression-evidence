# Attribution Summary

The upstream-facing claim is based on clean bare-metal timing.  The attribution
data here is supporting evidence only.

## Layer Timing

The bpftrace run instruments the `flistxattr(fd)` path and is not clean timing;
the probes add overhead.  It is useful only for seeing which layer grows with
the new kernel.

| function | old avg ns | new avg ns | delta |
| --- | ---: | ---: | ---: |
| listxattr | 1782.266 | 1876.309 | +5.28% |
| vfs_listxattr | 1495.954 | 1591.404 | +6.38% |
| security_inode_listxattr | 114.549 | 110.564 | -3.48% |
| shmem_listxattr | 1039.734 | 1140.212 | +9.66% |
| simple_xattr_list | 817.466 | 914.481 | +11.87% |
| posix_acl_listxattr | 112.720 | 111.081 | -1.45% |
| security_inode_listsecurity | 111.531 | 112.756 | +1.10% |
| xattr_list_one | 118.407 | 114.600 | -3.22% |

This weakens the explanation that the main regression is in the security hook
itself.  The growing part is closer to `shmem_listxattr()` /
`simple_xattr_list()`.

## Diagnostic Source Probes

These probes were used only to test mechanism hypotheses.  They are not
upstream patches.

| probe | result | interpretation |
| --- | --- | --- |
| fixed-name `flistxattr` fastpath | v7.1 moved from about 222-227 ns/op to about 150 ns/op | skipping the rhashtable walk removes most of the small-list cost, but the probe changes multi-xattr list semantics |
| rbtree-backend probe | v7.1 moved from about 222 ns/op to about 131 ns/op | switching the backend shape back toward the old rbtree path removes the measured fixed cost for this workload |

Together with the exact parent/child A/B, this supports the mechanism:

```text
tmpfs small-list flistxattr(fd) pays a higher fixed enumeration cost after
tmpfs switches to lazy rhashtable-based simple_xattrs.
```

