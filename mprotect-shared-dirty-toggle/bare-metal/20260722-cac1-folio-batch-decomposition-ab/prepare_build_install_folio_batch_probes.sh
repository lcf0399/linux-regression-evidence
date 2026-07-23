#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOT=${ROOT:-/home/lcf/kernel-study}
SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
GENERIC=$SCRIPT_DIR/build_probe_from_exact_child.sh
EXACT_BASE=${EXACT_BASE:-$ROOT/linux-baremetal/mprotect-cac1-exact-20260721}
FASTPATH_BASE=${FASTPATH_BASE:-$ROOT/linux-baremetal/mprotect-cac1-ptefast-20260722}
FOLIOONLY_BASE=${FOLIOONLY_BASE:-$ROOT/linux-baremetal/mprotect-cac1-folioonly-20260722}
NOFOLIO_BASE=${NOFOLIO_BASE:-$ROOT/linux-baremetal/mprotect-cac1-nofolio-direct-20260722}
MATRIX_BASE=${MATRIX_BASE:-$ROOT/linux-baremetal/mprotect-cac1-folio-decomp-20260722}

fail()
{
	echo "folio/batch probe preparation failed: $*" >&2
	exit 1
}

manifest_value()
{
	local manifest=$1
	local key=$2
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$manifest"
}

run_probe()
{
	local base=$1
	local patch_file=$2
	local localversion=$3
	local diagnostic=$4
	local marker=$5
	local gate=$6
	local status=$base/build-logs/matrix-status.env

	if [[ -s "$status" && $(manifest_value "$status" status) == complete ]]; then
		echo "reusing completed probe build: $base"
		return
	fi

	BASE="$base" PATCH_FILE="$patch_file" \
		PROBE_LOCALVERSION="$localversion" DIAGNOSTIC="$diagnostic" \
		SOURCE_MARKER="$marker" AUDIT_GATE="$gate" \
		"$GENERIC"
}

[[ -x "$GENERIC" ]] || fail "missing generic builder $GENERIC"

run_probe \
	"$FASTPATH_BASE" \
	"$SCRIPT_DIR/0000-diagnostic-single-pte-parent-style-commit-fastpath.patch" \
	-mprotect-cac-v-ptefastpath0 \
	single-pte-parent-style-commit-fastpath \
	'/* Diagnostic single-PTE fast path. */' \
	single-pte-fastpath

run_probe \
	"$FOLIOONLY_BASE" \
	"$SCRIPT_DIR/0001-diagnostic-keep-folio-skip-batch-direct-single-pte.patch" \
	-mprotect-cac-v-folioonly000 \
	keep-folio-skip-normal-batch-direct-single-pte \
	'Diagnostic: keep the folio lookup, skip normal-path batching.' \
	folio-only

run_probe \
	"$NOFOLIO_BASE" \
	"$SCRIPT_DIR/0002-diagnostic-skip-folio-and-batch-direct-single-pte.patch" \
	-mprotect-cac-v-nofolio00000 \
	skip-folio-and-normal-batch-direct-single-pte \
	'Diagnostic: skip normal-path folio lookup and batching.' \
	no-folio

mkdir -p "$MATRIX_BASE"/{build-logs,manifests,workload}
cp "$EXACT_BASE/build-logs/parent.artifacts.tsv" \
	"$MATRIX_BASE/build-logs/parent.artifacts.tsv"
cp "$EXACT_BASE/build-logs/child.artifacts.tsv" \
	"$MATRIX_BASE/build-logs/child.artifacts.tsv"

rewrite_role()
{
	local input=$1
	local output=$2
	local role=$3
	awk -F '\t' -v OFS='\t' -v role="$role" \
		'$1 == "role" { $2 = role } { print }' "$input" > "$output"
}

rewrite_role "$FASTPATH_BASE/build-logs/probe.artifacts.tsv" \
	"$MATRIX_BASE/build-logs/fastpath.artifacts.tsv" fastpath
rewrite_role "$FOLIOONLY_BASE/build-logs/probe.artifacts.tsv" \
	"$MATRIX_BASE/build-logs/folioonly.artifacts.tsv" folioonly
rewrite_role "$NOFOLIO_BASE/build-logs/probe.artifacts.tsv" \
	"$MATRIX_BASE/build-logs/nofolio.artifacts.tsv" nofolio
rewrite_role "$FASTPATH_BASE/manifests/probe.source.tsv" \
	"$MATRIX_BASE/manifests/fastpath.source.tsv" fastpath
rewrite_role "$FOLIOONLY_BASE/manifests/probe.source.tsv" \
	"$MATRIX_BASE/manifests/folioonly.source.tsv" folioonly
rewrite_role "$NOFOLIO_BASE/manifests/probe.source.tsv" \
	"$MATRIX_BASE/manifests/nofolio.source.tsv" nofolio

cp "$EXACT_BASE/workload/mprotect_shared_dirty_reproducer.c" "$MATRIX_BASE/workload/"
cp "$FASTPATH_BASE/build-logs/probe.change_pte_range.objdump.txt" \
	"$MATRIX_BASE/build-logs/fastpath.change_pte_range.objdump.txt"
cp "$FOLIOONLY_BASE/build-logs/probe.change_pte_range.objdump.txt" \
	"$MATRIX_BASE/build-logs/folioonly.change_pte_range.objdump.txt"
cp "$NOFOLIO_BASE/build-logs/probe.change_pte_range.objdump.txt" \
	"$MATRIX_BASE/build-logs/nofolio.change_pte_range.objdump.txt"

printf 'role\tkernelrelease\tcanonical_config_sha256\tdiagnostic\n' \
	> "$MATRIX_BASE/build-logs/matrix-artifacts.tsv"
for role in parent child fastpath folioonly nofolio; do
	manifest="$MATRIX_BASE/build-logs/$role.artifacts.tsv"
	printf '%s\t%s\t%s\t%s\n' \
		"$role" \
		"$(manifest_value "$manifest" kernelrelease)" \
		"$(manifest_value "$manifest" canonical_config_sha256)" \
		"$(manifest_value "$manifest" diagnostic)" \
		>> "$MATRIX_BASE/build-logs/matrix-artifacts.tsv"
done

EXPECTED_CANONICAL=$(manifest_value "$MATRIX_BASE/build-logs/parent.artifacts.tsv" \
	canonical_config_sha256)
awk -F '\t' -v expected="$EXPECTED_CANONICAL" \
	'NR > 1 && $3 != expected { bad++ } END { exit bad != 0 }' \
	"$MATRIX_BASE/build-logs/matrix-artifacts.tsv" ||
	fail "one or more matrix kernels use a different canonical config"

cat > "$MATRIX_BASE/build-logs/matrix-status.env" <<EOF
status	complete
matrix_design	parent-a,child-a,fastpath-a,folioonly-a,nofolio,folioonly-b,fastpath-b,child-b,parent-b
required_kernel_cmdline	preempt=none
completed_utc	$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

cat "$MATRIX_BASE/build-logs/matrix-artifacts.tsv"
cat "$MATRIX_BASE/build-logs/matrix-status.env"
