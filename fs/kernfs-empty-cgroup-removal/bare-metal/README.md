# Bare-metal experiment index

Each directory is a separate evidence bundle with its own identities,
measurements and verification. Do not pool samples across bundles.

| Directory | Scope |
| --- | --- |
| [original-regression](original-regression/README.md) | Original empty-cgroup workload research: two exact-source regression runs, the early root-reuse prototype and the pinned-mainline comparison; 603 invocation samples |
| [inode-inited](inode-inited/README.md) | T.J.'s first patch validation, with a separately recorded pacing diagnostic |
| [inode-requested](inode-requested/README.md) | Independent inode-request marker: matched patch comparison, correctness checks, memory cost and remaining limits |

The first bundle includes several experiments using the original workload
family, not one merged experiment. Root reuse changes the kernel; the mainline
comparison changes the source version while retaining the disabled control.
These are not newly generated workload candidates.

Run each bundle's `verify.py` from its directory; the pacing diagnostic also
has its own verifier. The layout change does not alter results or existing
immutable GitHub links. [中文说明](README.zh-CN.md).
