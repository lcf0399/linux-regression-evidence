#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=${ROOT:-/home/lcf/kernel-study}
BASE=${BASE:-$ROOT/linux-baremetal/mprotect-v713-shared-hint-20260722}
SOURCE=${SOURCE:-$SCRIPT_DIR/mprotect_shared_pte_mapped_thp_reproducer.c}
PIN_CPU=${PIN_CPU:-2}
SHMEM_CONTROL=/sys/kernel/mm/transparent_hugepage/shmem_enabled

fail()
{
	echo "large-folio shape smoke failed: $*" >&2
	exit 1
}

sudo -n true || fail "passwordless sudo is required"
[[ -s "$SOURCE" ]] || fail "missing reproducer source $SOURCE"
[[ -r "$SHMEM_CONTROL" ]] || fail "missing $SHMEM_CONTROL"

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
OUT="$BASE/large-folio-shape-smoke/${STAMP}_$(uname -r)"
BIN="$BASE/bin/mprotect_shared_pte_mapped_thp_reproducer"
mkdir -p "$OUT" "$BASE/bin"
SHMEM_BEFORE=$(<"$SHMEM_CONTROL")
SHMEM_SELECTED=$(printf '%s\n' "$SHMEM_BEFORE" | sed -n 's/.*\[\([^]]*\)\].*/\1/p')
[[ -n "$SHMEM_SELECTED" ]] || fail "cannot parse selected shmem THP mode"

restore_shmem()
{
	printf '%s\n' "$SHMEM_SELECTED" | sudo tee "$SHMEM_CONTROL" >/dev/null || true
}
trap restore_shmem EXIT

gcc -O2 -Wall -Wextra -Werror -o "$BIN" "$SOURCE"
printf '%s\n' advise | sudo tee "$SHMEM_CONTROL" >/dev/null
sudo env ITERATIONS=1 WARMUP=0 taskset -c "$PIN_CPU" "$BIN" |
	tee "$OUT/raw.log"
grep -Eq 'expected_match_ratio=100 .*unexpected_results=0' "$OUT/raw.log" ||
	fail "PTE-mapped large-folio shape was not established"

{
	printf 'timestamp_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'kernel_release=%s\n' "$(uname -r)"
	printf 'pin_cpu=%s\n' "$PIN_CPU"
	printf 'shmem_enabled_before=%s\n' "$SHMEM_BEFORE"
	printf 'reproducer_source_sha256=%s\n' "$(sha256sum "$SOURCE" | awk '{ print $1 }')"
	printf 'reproducer_binary_sha256=%s\n' "$(sha256sum "$BIN" | awk '{ print $1 }')"
} > "$OUT/env.txt"

restore_shmem
trap - EXIT
[[ $(<"$SHMEM_CONTROL") == "$SHMEM_BEFORE" ]] || fail "shmem THP mode was not restored"
echo "large_folio_shape_smoke=pass"
echo "smoke_dir=$OUT"
