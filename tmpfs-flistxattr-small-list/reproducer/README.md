# Reproducer

`xattr_smoke.c` is the standalone workload source used for the xattr
experiments.

For the upstream-facing result, the relevant scenarios are:

```text
XATTR_SCENARIO=flistxattr_fd
XATTR_SCENARIO=flistxattr_fd_count
```

Typical command shape:

```bash
gcc -O2 -Wall -Wextra -o xattr_smoke xattr_smoke.c
TEST_DIR=/tmp XATTR_SCENARIO=flistxattr_fd ITERATIONS=65536 ./xattr_smoke
TEST_DIR=/tmp XATTR_SCENARIO=flistxattr_fd_count XATTR_COUNT=4 ITERATIONS=65536 ./xattr_smoke
```

The benchmark should be run on tmpfs for this claim.  `flistxattr_fd` checks
that the returned xattr list is non-empty.  `flistxattr_fd_count` seeds a fixed
number of `user.fsregression.NNN` xattrs and checks that the returned list
length matches the exact expected length.

