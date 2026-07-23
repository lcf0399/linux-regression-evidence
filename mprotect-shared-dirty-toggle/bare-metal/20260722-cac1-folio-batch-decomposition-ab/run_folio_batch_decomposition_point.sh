#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=${ROOT:-/home/lcf/kernel-study}
export BASE=${BASE:-$ROOT/linux-baremetal/mprotect-cac1-folio-decomp-20260722}
export REPRODUCER_SOURCE=${REPRODUCER_SOURCE:-$BASE/workload/mprotect_shared_dirty_reproducer.c}

exec "$SCRIPT_DIR/../20260721-cac1db8c3aad-exact-ab/run_exact_ab_point.sh" "$@"
