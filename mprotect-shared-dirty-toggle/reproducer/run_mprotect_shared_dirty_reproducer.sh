#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

cd "$(dirname "$0")"

: "${CC:=gcc}"
: "${MAPPING_MB:=64}"
: "${ITERATIONS:=1000}"
: "${WARMUP:=10}"
: "${EXTERNAL_ROUNDS:=15}"
: "${OUT:=/tmp/mprotect_shared_dirty_reproducer}"

"$CC" -O2 -Wall -Wextra -o "$OUT" \
  mprotect_shared_dirty_reproducer.c

exec "$OUT" \
  shared_dirty_full_toggle_64m "$EXTERNAL_ROUNDS" \
  --mapping-mb "$MAPPING_MB" \
  --iterations "$ITERATIONS" \
  --warmup "$WARMUP"
