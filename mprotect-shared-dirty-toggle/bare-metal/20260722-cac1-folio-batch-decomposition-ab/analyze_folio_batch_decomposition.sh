#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BASE=${BASE:-$SCRIPT_DIR}
RUNS=${RUNS:-$BASE/runs}

fail()
{
	echo "folio/batch decomposition analysis failed: $*" >&2
	exit 1
}

env_value()
{
	local file=$1
	local key=$2
	awk -F= -v key="$key" '$1 == key { sub(/^[^=]*=/, ""); print; exit }' "$file"
}

select_run()
{
	local label=$1
	find "$RUNS" -mindepth 1 -maxdepth 1 -type d -name "*_${label}_*" -print |
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
		END {
			if (!n) exit 1
			printf "%.9f", sum/n
		}
	' "$file"
}

mkdir -p "$BASE"
[[ -d "$RUNS" ]] || fail "missing runs directory $RUNS"

POINTS=(parent-a child-a fastpath-a folioonly-a nofolio folioonly-b fastpath-b child-b parent-b)
EXPECTED_ROLES=(parent child fastpath folioonly nofolio folioonly fastpath child parent)
declare -a DIRS=()
for point in "${POINTS[@]}"; do
	dir=$(select_run "$point")
	[[ -n "$dir" ]] || fail "missing $point run"
	DIRS+=("$dir")
done

printf 'order\tpoint\trole\tcommit\tn\tmean_iteration_ns_per_page\tsd\tcv_pct\tvalues\tsemantic_failures\n' > "$BASE/summary.tsv"
printf 'point\trun_dir\n' > "$BASE/selected-runs.tsv"
printf 'point\tkernel_release\tboot_id\tfailed_units\tmeasured_rows\tsemantic_failures\n' > "$BASE/run-audit.tsv"

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

	[[ "$role" == "$expected_role" ]] ||
		fail "$point role is $role, expected $expected_role"
	[[ "$rows" == 15 ]] || fail "$point has $rows measured rows, expected 15"
	[[ "$semantic_failures" == 0 ]] || fail "$point has semantic failures"
	[[ "$failed_units" == 0 ]] || fail "$point has failed systemd units"
	awk -F '\t' 'NR == 1 { next }
		$6 != 100 || $7 != 0 || $8 != 4 || $9 != 4 || $10 != 0 { bad++ }
		END { exit bad != 0 }' "$dir/measurements.tsv" ||
		fail "$point failed returned-value or page-state gates"

	printf '%d\t%s\t%s\t%s\t' "$((index + 1))" "$point" "$role" "$commit" >> "$BASE/summary.tsv"
	awk -F '\t' 'NR == 2 {
		printf "%s\t%s\t%s\t%s\t%s\t%s\n", $4, $5, $6, $7, $8, $9
	}' "$dir/summary.tsv" >> "$BASE/summary.tsv"
	printf '%s\t%s\n' "$point" "$(basename "$dir")" >> "$BASE/selected-runs.tsv"
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$point" "$kernel" "$boot_id" "$failed_units" "$rows" "$semantic_failures" \
		>> "$BASE/run-audit.tsv"
	BOOT_IDS+=("$boot_id")
done

for ((i = 0; i < ${#BOOT_IDS[@]}; i++)); do
	for ((j = i + 1; j < ${#BOOT_IDS[@]}; j++)); do
		[[ ${BOOT_IDS[$i]} != "${BOOT_IDS[$j]}" ]] ||
			fail "fresh-boot IDs are not unique"
	done
done

printf 'analysis\tparent_a\tchild_a\tfastpath_a\tfolioonly_a\tnofolio\tfolioonly_b\tfastpath_b\tchild_b\tparent_b\tparent_midpoint\tchild_midpoint\tfastpath_midpoint\tfolioonly_midpoint\toriginal_regression_pct\tfastpath_vs_child_pct\tfolioonly_vs_fastpath_pct\tnofolio_vs_folioonly_pct\tnofolio_vs_parent_pct\tparent_drift_pct\tchild_drift_pct\tfastpath_drift_pct\tfolioonly_drift_pct\tcommit_path_recovery_pct\tbatch_discovery_recovery_pct\tfolio_lookup_recovery_pct\tcombined_recovery_pct\tresidual_gap_pct\n' > "$BASE/sensitivity.tsv"

for analysis in all_15_rounds drop_first_round; do
	drop_first=0
	[[ "$analysis" == drop_first_round ]] && drop_first=1
	declare -a means=()
	for dir in "${DIRS[@]}"; do
		means+=("$(mean_column "$dir/measurements.tsv" 5 "$drop_first")")
	done

	awk -v analysis="$analysis" \
		-v pa="${means[0]}" -v ca="${means[1]}" \
		-v fa="${means[2]}" -v foa="${means[3]}" -v nf="${means[4]}" \
		-v fob="${means[5]}" -v fb="${means[6]}" \
		-v cb="${means[7]}" -v pb="${means[8]}" 'BEGIN {
		pmid=(pa+pb)/2
		cmid=(ca+cb)/2
		fmid=(fa+fb)/2
		fomid=(foa+fob)/2
		gap=cmid-pmid
		if (gap <= 0) exit 2
		printf "%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n", \
			analysis, pa, ca, fa, foa, nf, fob, fb, cb, pb,
			pmid, cmid, fmid, fomid,
			100*(cmid/pmid-1), 100*(fmid/cmid-1),
			100*(fomid/fmid-1), 100*(nf/fomid-1), 100*(nf/pmid-1),
			100*(pb/pa-1), 100*(cb/ca-1), 100*(fb/fa-1), 100*(fob/foa-1),
			100*(cmid-fmid)/gap, 100*(fmid-fomid)/gap,
			100*(fomid-nf)/gap, 100*(cmid-nf)/gap,
			100*(nf-pmid)/gap
	}' >> "$BASE/sensitivity.tsv" || fail "$analysis has no positive original gap"
done

printf 'point\tmean_protect_ns_per_page\tmean_restore_ns_per_page\tmean_post_touch_ns_per_page\tmean_iteration_ns_per_page\n' > "$BASE/component-summary.tsv"
for index in "${!POINTS[@]}"; do
	point=${POINTS[$index]}
	dir=${DIRS[$index]}
	printf '%s\t%.6f\t%.6f\t%.6f\t%.6f\n' \
		"$point" \
		"$(mean_column "$dir/measurements.tsv" 2 0)" \
		"$(mean_column "$dir/measurements.tsv" 3 0)" \
		"$(mean_column "$dir/measurements.tsv" 4 0)" \
		"$(mean_column "$dir/measurements.tsv" 5 0)" \
		>> "$BASE/component-summary.tsv"
done

MAX_CV=$(awk -F '\t' 'NR > 1 && $8 > max { max=$8 } END { printf "%.6f", max+0 }' "$BASE/summary.tsv")
read -r ALL_REG ALL_RESID ALL_PD ALL_CD ALL_FD ALL_FOD ALL_COMMIT ALL_BATCH ALL_FOLIO ALL_COMBINED ALL_REMAIN < <(
	awk -F '\t' '$1 == "all_15_rounds" { print $15, $19, $20, $21, $22, $23, $24, $25, $26, $27, $28 }' "$BASE/sensitivity.tsv")
read -r DROP_REG DROP_RESID DROP_PD DROP_CD DROP_FD DROP_FOD DROP_COMMIT DROP_BATCH DROP_FOLIO DROP_COMBINED DROP_REMAIN < <(
	awk -F '\t' '$1 == "drop_first_round" { print $15, $19, $20, $21, $22, $23, $24, $25, $26, $27, $28 }' "$BASE/sensitivity.tsv")

awk -v max_cv="$MAX_CV" \
	-v ar="$ALL_REG" -v az="$ALL_RESID" -v apd="$ALL_PD" -v acd="$ALL_CD" \
	-v afd="$ALL_FD" -v afod="$ALL_FOD" -v ac="$ALL_COMMIT" \
	-v ab="$ALL_BATCH" -v af="$ALL_FOLIO" -v acomb="$ALL_COMBINED" -v arem="$ALL_REMAIN" \
	-v dr="$DROP_REG" -v dz="$DROP_RESID" -v dpd="$DROP_PD" -v dcd="$DROP_CD" \
	-v dfd="$DROP_FD" -v dfod="$DROP_FOD" -v dc="$DROP_COMMIT" \
	-v db="$DROP_BATCH" -v df="$DROP_FOLIO" -v dcomb="$DROP_COMBINED" -v drem="$DROP_REMAIN" '
	function abs(x) { return x < 0 ? -x : x }
	BEGIN {
		valid=(max_cv <= 5 && ar >= 20 && dr >= 20 &&
		       abs(apd) <= 3 && abs(acd) <= 3 && abs(afd) <= 3 && abs(afod) <= 3 &&
		       abs(dpd) <= 3 && abs(dcd) <= 3 && abs(dfd) <= 3 && abs(dfod) <= 3)
		major=(valid && acomb >= 75 && dcomb >= 75 && abs(az) <= 10 && abs(dz) <= 10)
		min_combined=(acomb < dcomb ? acomb : dcomb)
		if (!valid)
			classification="invalid-needs-review"
		else if (major)
			classification="combined-major-explanation"
		else if (min_combined >= 25)
			classification="combined-partial-explanation"
		else
			classification="combined-minor-or-no-recovery"
		print "validity\tclassification\tmax_cv_pct\tall_original_regression_pct\tall_nofolio_vs_parent_pct\tall_commit_recovery_pct\tall_batch_recovery_pct\tall_folio_recovery_pct\tall_combined_recovery_pct\tall_residual_gap_pct\tdrop_original_regression_pct\tdrop_nofolio_vs_parent_pct\tdrop_commit_recovery_pct\tdrop_batch_recovery_pct\tdrop_folio_recovery_pct\tdrop_combined_recovery_pct\tdrop_residual_gap_pct"
		printf "%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n", \
			(valid ? "pass" : "fail"), classification, max_cv,
			ar, az, ac, ab, af, acomb, arem,
			dr, dz, dc, db, df, dcomb, drem
	}' > "$BASE/decision.tsv"

cat "$BASE/summary.tsv"
cat "$BASE/sensitivity.tsv"
cat "$BASE/component-summary.tsv"
cat "$BASE/decision.tsv"
echo "analysis_complete=yes"
