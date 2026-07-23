#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BASE=${BASE:-$SCRIPT_DIR}
RUNS=${RUNS:-$BASE/runs}
TRACES=${TRACES:-$BASE/runtime-trace}

fail()
{
	echo "shared-hint analysis failed: $*" >&2
	exit 1
}

env_value()
{
	local file=$1
	local key=$2
	awk -F= -v key="$key" '$1 == key { sub(/^[^=]*=/, ""); print; exit }' "$file"
}

select_dir()
{
	local root=$1
	local label=$2
	find "$root" -mindepth 1 -maxdepth 1 -type d -name "*${label}*" -print |
		sort | tail -n 1
}

mean_column()
{
	local file=$1
	local column=$2
	local drop_first=$3
	awk -F '\t' -v column="$column" -v drop_first="$drop_first" '
		NR == 1 { next }
		drop_first && NR == 2 { next }
		{ sum += $column; n++ }
		END { if (!n) exit 1; printf "%.9f", sum/n }
	' "$file"
}

POINTS=(baseline-a candidate baseline-b)
EXPECTED_ROLES=(baseline candidate baseline)
declare -a DIRS=()
for point in "${POINTS[@]}"; do
	dir=$(select_dir "$RUNS" "_${point}_")
	[[ -n "$dir" ]] || fail "missing $point run"
	DIRS+=("$dir")
done

printf 'order\tpoint\trole\tcommit\tn\tmean_iteration_ns_per_page\tsd\tcv_pct\tvalues\tsemantic_failures\n' \
	> "$BASE/summary.tsv"
printf 'point\trun_dir\n' > "$BASE/selected-runs.tsv"
printf 'point\tkernel_release\tboot_id\tfailed_units\tmeasured_rows\tsemantic_failures\n' \
	> "$BASE/run-audit.tsv"

declare -a BOOT_IDS=()
for index in "${!POINTS[@]}"; do
	point=${POINTS[$index]}
	expected_role=${EXPECTED_ROLES[$index]}
	dir=${DIRS[$index]}
	for file in summary.tsv measurements.tsv env.txt; do
		[[ -s "$dir/$file" ]] || fail "missing $dir/$file"
	done
	role=$(env_value "$dir/env.txt" role)
	commit=$(env_value "$dir/env.txt" commit)
	kernel=$(env_value "$dir/env.txt" kernel_release)
	boot_id=$(env_value "$dir/env.txt" boot_id)
	failed_units=$(env_value "$dir/env.txt" failed_units)
	rows=$(awk 'END { print NR-1 }' "$dir/measurements.tsv")
	semantic_failures=$(awk -F '\t' 'NR == 2 { print $9 }' "$dir/summary.tsv")

	[[ "$role" == "$expected_role" ]] || fail "$point role is $role, expected $expected_role"
	[[ "$rows" == 15 ]] || fail "$point has $rows rows, expected 15"
	[[ "$semantic_failures" == 0 ]] || fail "$point has semantic failures"
	[[ "$failed_units" == 0 ]] || fail "$point has failed systemd units"
	awk -F '\t' 'NR == 1 { next }
		$6 != 100 || $7 != 0 || $8 != 4 || $9 != 4 || $10 != 0 { bad++ }
		END { exit bad != 0 }' "$dir/measurements.tsv" ||
		fail "$point failed returned-value or page-state gates"

	printf '%d\t%s\t%s\t%s\t' "$((index + 1))" "$point" "$role" "$commit" \
		>> "$BASE/summary.tsv"
	awk -F '\t' 'NR == 2 { printf "%s\t%s\t%s\t%s\t%s\t%s\n", $4, $5, $6, $7, $8, $9 }' \
		"$dir/summary.tsv" >> "$BASE/summary.tsv"
	printf '%s\t%s\n' "$point" "$(basename "$dir")" >> "$BASE/selected-runs.tsv"
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$point" "$kernel" "$boot_id" \
		"$failed_units" "$rows" "$semantic_failures" >> "$BASE/run-audit.tsv"
	BOOT_IDS+=("$boot_id")
done

for ((i = 0; i < ${#BOOT_IDS[@]}; i++)); do
	for ((j = i + 1; j < ${#BOOT_IDS[@]}; j++)); do
		[[ ${BOOT_IDS[$i]} != "${BOOT_IDS[$j]}" ]] || fail "fresh-boot IDs are not unique"
	done
done

printf 'analysis\tbaseline_a\tcandidate\tbaseline_b\tbaseline_midpoint\tcandidate_vs_midpoint_pct\tbaseline_drift_pct\n' \
	> "$BASE/sensitivity.tsv"
for analysis in all_15_rounds drop_first_round; do
	drop_first=0
	[[ "$analysis" == drop_first_round ]] && drop_first=1
	ba=$(mean_column "${DIRS[0]}/measurements.tsv" 5 "$drop_first")
	ca=$(mean_column "${DIRS[1]}/measurements.tsv" 5 "$drop_first")
	bb=$(mean_column "${DIRS[2]}/measurements.tsv" 5 "$drop_first")
	awk -v analysis="$analysis" -v ba="$ba" -v ca="$ca" -v bb="$bb" 'BEGIN {
		mid=(ba+bb)/2
		printf "%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n", analysis,
			ba, ca, bb, mid, 100*(ca/mid-1), 100*(bb/ba-1)
	}' >> "$BASE/sensitivity.tsv"
done

printf 'role\tpoint_label\tkernel_release\tchange_pte_range\tvm_normal_page\tmprotect_folio_pte_batch\n' \
	> "$BASE/runtime-trace-summary.tsv"
for spec in baseline-a:baseline candidate:candidate; do
	label=${spec%%:*}
	role=${spec##*:}
	dir=$(select_dir "$TRACES" "${label}_")
	[[ -s "$dir/summary.tsv" ]] || fail "missing runtime trace for $label"
	awk -F '\t' 'NR == 2 { print }' "$dir/summary.tsv" >> "$BASE/runtime-trace-summary.tsv"
done

awk -F '\t' '
	$1 == "baseline" { bc=$4+0; bn=$5+0; bb=$6+0 }
	$1 == "candidate" { cc=$4+0; cn=$5+0; cb=$6+0 }
	END {
		batch_drop=bb-cb
		normal_drop=bn-cn
		# The timed command performs two 1,024-PTE protection walks.  Process
		# setup contributes a small common residue, so use paired differences.
		bad=(bc <= 0 || cc <= 0 || bb < 2048 || cb > 64 ||
		     batch_drop < 2048 || normal_drop < 1900 || normal_drop > 2200)
		exit bad
	}
' "$BASE/runtime-trace-summary.tsv" || fail "runtime paired direct-hit gate failed"

MAX_CV=$(awk -F '\t' 'NR > 1 && $8 > max { max=$8 } END { printf "%.6f", max+0 }' "$BASE/summary.tsv")
read -r ALL_DELTA ALL_DRIFT < <(
	awk -F '\t' '$1 == "all_15_rounds" { print $6, $7 }' "$BASE/sensitivity.tsv")
read -r DROP_DELTA DROP_DRIFT < <(
	awk -F '\t' '$1 == "drop_first_round" { print $6, $7 }' "$BASE/sensitivity.tsv")

awk -v max_cv="$MAX_CV" -v ad="$ALL_DELTA" -v abr="$ALL_DRIFT" \
	-v dd="$DROP_DELTA" -v dbr="$DROP_DRIFT" '
	function abs(x) { return x < 0 ? -x : x }
	BEGIN {
		valid=(max_cv <= 5 && abs(abr) <= 3 && abs(dbr) <= 3)
		useful=(valid && ad <= -10 && dd <= -10)
		print "validity\tclassification\tmax_cv_pct\tall_candidate_vs_midpoint_pct\tall_baseline_drift_pct\tdrop_candidate_vs_midpoint_pct\tdrop_baseline_drift_pct\truntime_direct_hit"
		printf "%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\tpass\n",
			(valid ? "pass" : "fail"),
			(!valid ? "invalid-needs-review" : (useful ? "base-page-fastpath-useful" : "base-page-fastpath-below-gate")),
			max_cv, ad, abr, dd, dbr
	}' > "$BASE/decision.tsv"

cat "$BASE/summary.tsv"
cat "$BASE/sensitivity.tsv"
cat "$BASE/runtime-trace-summary.tsv"
cat "$BASE/decision.tsv"
echo "analysis_complete=yes"
