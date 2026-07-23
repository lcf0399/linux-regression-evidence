#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOT=${ROOT:-/home/lcf/kernel-study}
export BASE=${BASE:-$ROOT/linux-baremetal/mprotect-v713-shared-hint-20260722}
export REPRODUCER_SOURCE=${REPRODUCER_SOURCE:-$BASE/workload/mprotect_shared_dirty_reproducer.c}

exec "$ROOT/linux-regression-evidence/mprotect-shared-dirty-toggle/bare-metal/20260721-cac1db8c3aad-exact-ab/run_exact_ab_point.sh" "$@"
