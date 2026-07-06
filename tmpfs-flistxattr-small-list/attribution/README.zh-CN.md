# 归因摘要

上游口径的主体依据是 clean bare-metal timing。这里的归因数据只作为辅助证据。

## Layer Timing

bpftrace 运行会 probe `flistxattr(fd)` 路径，所以不是 clean timing；probe 本身会加
开销。它只适合看哪一层随新内核一起变大。

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

这削弱了“主要是 security hook 变慢”的解释。随版本一起变大的位置更接近
`shmem_listxattr()` / `simple_xattr_list()`。

## Diagnostic Source Probes

这些 probe 只用于验证机制假设，不是上游 patch。

| probe | result | interpretation |
| --- | --- | --- |
| fixed-name `flistxattr` fastpath | v7.1 从约 222-227 ns/op 降到约 150 ns/op | 跳过 rhashtable walk 后，小列表成本基本消失，但该 probe 会改变多 xattr list 语义 |
| rbtree-backend probe | v7.1 从约 222 ns/op 降到约 131 ns/op | 把 backend 形态改回接近旧 rbtree path 后，该 workload 的固定成本消失 |

结合 exact parent/child A/B，目前支持的机制解释是：

```text
tmpfs 切到 lazy rhashtable-based simple_xattrs 后，
small-list flistxattr(fd) 付出了更高的固定枚举成本。
```

