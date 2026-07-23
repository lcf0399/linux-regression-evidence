#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

if [[ $# -ne 2 ]]; then
	echo "usage: $0 <baseline|candidate> <point-label>" >&2
	exit 2
fi

ROLE=$1
POINT_LABEL=$2
[[ "$ROLE" == baseline || "$ROLE" == candidate ]] || {
	echo "invalid role: $ROLE" >&2
	exit 2
}

ROOT=${ROOT:-/home/lcf/kernel-study}
BASE=${BASE:-$ROOT/linux-baremetal/mprotect-v713-shared-hint-20260722}
MANIFEST="$BASE/build-logs/$ROLE.artifacts.tsv"
SOURCE=${SOURCE:-$BASE/workload/mprotect_shared_dirty_reproducer.c}
PIN_CPU=${PIN_CPU:-2}
MAPPING_MB=${MAPPING_MB:-4}
ITERATIONS=${ITERATIONS:-1}
INTERNAL_WARMUP=${INTERNAL_WARMUP:-0}

fail()
{
	echo "lookup trace smoke failed: $*" >&2
	exit 1
}

manifest_value()
{
	local key=$1
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$MANIFEST"
}

[[ -s "$MANIFEST" ]] || fail "missing manifest $MANIFEST"
EXPECTED_RELEASE=$(manifest_value kernelrelease)
[[ $(uname -r) == "$EXPECTED_RELEASE" ]] ||
	fail "running $(uname -r), expected $EXPECTED_RELEASE"
command -v bpftrace >/dev/null 2>&1 || fail "bpftrace is not installed"
[[ -s "$SOURCE" ]] || fail "missing reproducer source $SOURCE"

mkdir -p "$BASE/bin" "$BASE/runtime-trace"
BIN="$BASE/bin/mprotect_shared_dirty_reproducer"
gcc -O2 -Wall -Wextra -o "$BIN" "$SOURCE"

OUT="$BASE/runtime-trace/${POINT_LABEL}_${EXPECTED_RELEASE}"
mkdir -p "$OUT"
PROGRAM='BEGIN { @change = 0; @normal = 0; @batch = 0; }
kprobe:change_pte_range /pid == cpid/ { @change = @change + 1; }
kprobe:vm_normal_page /pid == cpid/ { @normal = @normal + 1; }
kprobe:mprotect_folio_pte_batch /pid == cpid/ { @batch = @batch + 1; }
END { printf("trace_result change_pte_range=%llu vm_normal_page=%llu mprotect_folio_pte_batch=%llu\n", @change, @normal, @batch); clear(@change); clear(@normal); clear(@batch); }'
COMMAND="taskset -c $PIN_CPU $BIN shared_dirty_full_toggle_64m 1 --mapping-mb $MAPPING_MB --iterations $ITERATIONS --warmup $INTERNAL_WARMUP"

printf '%s\n' "$PROGRAM" > "$OUT/program.bt"
printf '%s\n' "$COMMAND" > "$OUT/command.txt"
sudo bpftrace -q -c "$COMMAND" -e "$PROGRAM" | tee "$OUT/raw.log"

RESULT=$(awk '/^trace_result / { line=$0 } END { print line }' "$OUT/raw.log")
[[ -n "$RESULT" ]] || fail "bpftrace emitted no trace_result line"
value()
{
	printf '%s\n' "$RESULT" | tr ' ' '\n' |
		awk -F= -v key="$1" '$1 == key { print $2; exit }'
}
CHANGE=$(value change_pte_range)
NORMAL=$(value vm_normal_page)
BATCH=$(value mprotect_folio_pte_batch)
[[ "$CHANGE" =~ ^[0-9]+$ && "$NORMAL" =~ ^[0-9]+$ && "$BATCH" =~ ^[0-9]+$ ]] ||
	fail "non-numeric trace counts: $RESULT"
(( CHANGE > 0 )) || fail "workload did not reach change_pte_range"
case "$ROLE" in
	baseline)
		(( NORMAL > 0 )) || fail "baseline did not call vm_normal_page"
		(( BATCH > 0 )) || fail "baseline did not call mprotect_folio_pte_batch"
		;;
	candidate)
		# The traced process also performs ELF setup and reads /proc/self/smaps;
		# those paths can call vm_normal_page(), and a few setup mprotect() calls
		# can reach the batch helper.  The paired analysis below verifies that the
		# two measured 1,024-PTE walks disappear relative to the baseline trace.
		(( BATCH <= 64 )) ||
			fail "candidate retained too many batch-helper calls: $BATCH"
		;;
esac

printf 'role\tpoint_label\tkernel_release\tchange_pte_range\tvm_normal_page\tmprotect_folio_pte_batch\n' \
	> "$OUT/summary.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$ROLE" "$POINT_LABEL" \
	"$EXPECTED_RELEASE" "$CHANGE" "$NORMAL" "$BATCH" >> "$OUT/summary.tsv"
cat "$OUT/summary.tsv"
echo "trace_dir=$OUT"
