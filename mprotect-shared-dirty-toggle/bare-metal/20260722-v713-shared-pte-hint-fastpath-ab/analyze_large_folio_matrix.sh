#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BASE=${BASE:-$SCRIPT_DIR}
RUNS=${RUNS:-$BASE/large-folio-runs}

fail()
{
	echo "large-folio analysis failed: $*" >&2
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
	local label=$1
	find "$RUNS" -mindepth 1 -maxdepth 1 -type d -name "*_${label}_*" -print |
		sort | tail -n 1
}

mean_mprotect()
{
	local file=$1
	local drop_first=$2
	awk -F '\t' -v drop_first="$drop_first" '
		NR == 1 { next }
		drop_first && NR == 2 { next }
		{ sum += ($2+0)+($3+0); n++ }
		END { if (!n) exit 1; printf "%.9f", sum/n }
	' "$file"
}

POINTS=(large-baseline-a large-candidate large-baseline-b)
EXPECTED_ROLES=(baseline candidate baseline)
declare -a DIRS=()
for point in "${POINTS[@]}"; do
	dir=$(select_dir "$point")
	[[ -n "$dir" ]] || fail "missing $point run"
	DIRS+=("$dir")
done

printf 'order\tpoint\trole\tcommit\tn\tmean_mprotect_ns_per_page\tmean_protect_ns_per_page\tmean_restore_ns_per_page\tmean_touch_ns_per_page\tmean_iteration_ns_per_page\tsd_mprotect\tcv_mprotect_pct\tvalues_mprotect\tsemantic_failures\n' \
	> "$BASE/large-folio-summary.tsv"
printf 'point\trun_dir\n' > "$BASE/large-folio-selected-runs.tsv"
printf 'point\tkernel_release\tboot_id\tfailed_units\tmeasured_rows\tsemantic_failures\n' \
	> "$BASE/large-folio-run-audit.tsv"

declare -a BOOT_IDS=()
for index in "${!POINTS[@]}"; do
	point=${POINTS[$index]}
	expected_role=${EXPECTED_ROLES[$index]}
	dir=${DIRS[$index]}
	for file in summary.tsv measurements.tsv env.txt availability.tsv; do
		[[ -s "$dir/$file" ]] || fail "missing $dir/$file"
	done
	grep -Fqx $'availability\tavailable' "$dir/availability.tsv" ||
		fail "$point did not establish the large-folio shape"
	role=$(env_value "$dir/env.txt" role)
	commit=$(env_value "$dir/env.txt" commit)
	kernel=$(env_value "$dir/env.txt" kernel_release)
	boot_id=$(env_value "$dir/env.txt" boot_id)
	failed_units=$(env_value "$dir/env.txt" failed_units)
	rows=$(awk 'END { print NR-1 }' "$dir/measurements.tsv")
	semantic_failures=$(awk -F '\t' 'NR == 2 { print $13 }' "$dir/summary.tsv")

	[[ "$role" == "$expected_role" ]] || fail "$point role is $role, expected $expected_role"
	[[ "$rows" == 15 ]] || fail "$point has $rows rows, expected 15"
	[[ "$semantic_failures" == 0 ]] || fail "$point has semantic failures"
	[[ "$failed_units" == 0 ]] || fail "$point has failed systemd units"
	awk -F '\t' 'NR == 1 { next }
		$6 != 100 || $7 != 0 || $8 < 2048 || $9 < 1 || $10 < 1 ||
		$11 < 1 || $12 < 1 { bad++ }
		END { exit bad != 0 }' "$dir/measurements.tsv" ||
		fail "$point failed large-folio shape or semantic gates"

	printf '%d\t%s\t%s\t%s\t' "$((index + 1))" "$point" "$role" "$commit" \
		>> "$BASE/large-folio-summary.tsv"
	awk -F '\t' 'NR == 2 { printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", $4, $5, $6, $7, $8, $9, $10, $11, $12, $13 }' \
		"$dir/summary.tsv" >> "$BASE/large-folio-summary.tsv"
	printf '%s\t%s\n' "$point" "$(basename "$dir")" \
		>> "$BASE/large-folio-selected-runs.tsv"
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$point" "$kernel" "$boot_id" \
		"$failed_units" "$rows" "$semantic_failures" \
		>> "$BASE/large-folio-run-audit.tsv"
	BOOT_IDS+=("$boot_id")
done

for ((i = 0; i < ${#BOOT_IDS[@]}; i++)); do
	for ((j = i + 1; j < ${#BOOT_IDS[@]}; j++)); do
		[[ ${BOOT_IDS[$i]} != "${BOOT_IDS[$j]}" ]] ||
			fail "fresh-boot IDs are not unique"
	done
done

printf 'analysis\tbaseline_a\tcandidate\tbaseline_b\tbaseline_midpoint\tcandidate_vs_midpoint_pct\tbaseline_drift_pct\n' \
	> "$BASE/large-folio-sensitivity.tsv"
for analysis in all_15_rounds drop_first_round; do
	drop_first=0
	[[ "$analysis" == drop_first_round ]] && drop_first=1
	ba=$(mean_mprotect "${DIRS[0]}/measurements.tsv" "$drop_first")
	ca=$(mean_mprotect "${DIRS[1]}/measurements.tsv" "$drop_first")
	bb=$(mean_mprotect "${DIRS[2]}/measurements.tsv" "$drop_first")
	awk -v analysis="$analysis" -v ba="$ba" -v ca="$ca" -v bb="$bb" 'BEGIN {
		mid=(ba+bb)/2
		printf "%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n", analysis,
			ba, ca, bb, mid, 100*(ca/mid-1), 100*(bb/ba-1)
	}' >> "$BASE/large-folio-sensitivity.tsv"
done

MAX_CV=$(awk -F '\t' 'NR > 1 && $12 > max { max=$12 } END { printf "%.6f", max+0 }' \
	"$BASE/large-folio-summary.tsv")
read -r ALL_DELTA ALL_DRIFT < <(
	awk -F '\t' '$1 == "all_15_rounds" { print $6, $7 }' \
		"$BASE/large-folio-sensitivity.tsv")
read -r DROP_DELTA DROP_DRIFT < <(
	awk -F '\t' '$1 == "drop_first_round" { print $6, $7 }' \
		"$BASE/large-folio-sensitivity.tsv")

awk -v max_cv="$MAX_CV" -v ad="$ALL_DELTA" -v abr="$ALL_DRIFT" \
	-v dd="$DROP_DELTA" -v dbr="$DROP_DRIFT" '
	function abs(x) { return x < 0 ? -x : x }
	BEGIN {
		valid=(max_cv <= 5 && abs(abr) <= 3 && abs(dbr) <= 3)
		safe=(valid && ad <= 5 && dd <= 5)
		print "validity\tclassification\tmax_cv_pct\tall_candidate_vs_midpoint_pct\tall_baseline_drift_pct\tdrop_candidate_vs_midpoint_pct\tdrop_baseline_drift_pct\treverse_gate"
		printf "%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%s\n",
			(valid ? "pass" : "fail"),
			(!valid ? "invalid-needs-review" : (safe ? "no-large-folio-regression" : "large-folio-regression")),
			max_cv, ad, abr, dd, dbr, (safe ? "pass" : "fail")
	}' > "$BASE/large-folio-decision.tsv"

cat "$BASE/large-folio-summary.tsv"
cat "$BASE/large-folio-sensitivity.tsv"
cat "$BASE/large-folio-decision.tsv"
echo "large_folio_analysis_complete=yes"
