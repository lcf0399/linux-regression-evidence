#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
BASE=${BASE:-$SCRIPT_DIR}
RUNS=${RUNS:-$BASE/runs}

fail()
{
	echo "Pedro v3 A/B analysis failed: $*" >&2
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

PARENT_A=$(select_run parent-a)
CHILD=$(select_run child)
PARENT_B=$(select_run parent-b)
[[ -n "$PARENT_A" ]] || fail "missing parent-a run"
[[ -n "$CHILD" ]] || fail "missing child run"
[[ -n "$PARENT_B" ]] || fail "missing parent-b run"

declare -a POINTS=(parent-a child parent-b)
declare -a DIRS=("$PARENT_A" "$CHILD" "$PARENT_B")

printf 'order\tpoint\trole\tcommit\tn\tmean_iteration_ns_per_page\tsd\tcv_pct\tvalues\tsemantic_failures\n' > "$BASE/summary.tsv"
printf 'point\trun_dir\n' > "$BASE/selected-runs.tsv"
printf 'point\tkernel_release\tboot_id\tfailed_units\tmeasured_rows\tsemantic_failures\n' > "$BASE/run-audit.tsv"

declare -a BOOT_IDS=()
for index in "${!POINTS[@]}"; do
	point=${POINTS[$index]}
	dir=${DIRS[$index]}
	[[ -s "$dir/summary.tsv" ]] || fail "missing $dir/summary.tsv"
	[[ -s "$dir/measurements.tsv" ]] || fail "missing $dir/measurements.tsv"
	[[ -s "$dir/env.txt" ]] || fail "missing $dir/env.txt"

	role=$(env_value "$dir/env.txt" role)
	commit=$(env_value "$dir/env.txt" commit)
	kernel=$(env_value "$dir/env.txt" kernel_release)
	boot_id=$(env_value "$dir/env.txt" boot_id)
	failed_units=$(env_value "$dir/env.txt" failed_units)
	rows=$(awk 'END { print NR-1 }' "$dir/measurements.tsv")
	semantic_failures=$(awk -F '\t' 'NR == 2 { print $9 }' "$dir/summary.tsv")

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

[[ ${BOOT_IDS[0]} != "${BOOT_IDS[1]}" && ${BOOT_IDS[0]} != "${BOOT_IDS[2]}" &&
	${BOOT_IDS[1]} != "${BOOT_IDS[2]}" ]] || fail "fresh-boot IDs are not unique"

printf 'analysis\tparent_a_mean\tchild_mean\tparent_b_mean\tparent_midpoint\tchild_vs_parent_midpoint_pct\tparent_b_vs_parent_a_pct\n' > "$BASE/sensitivity.tsv"
for analysis in all_15_rounds drop_first_round; do
	drop_first=0
	[[ "$analysis" == drop_first_round ]] && drop_first=1
	pa=$(mean_column "$PARENT_A/measurements.tsv" 5 "$drop_first")
	child=$(mean_column "$CHILD/measurements.tsv" 5 "$drop_first")
	pb=$(mean_column "$PARENT_B/measurements.tsv" 5 "$drop_first")
	awk -v analysis="$analysis" -v pa="$pa" -v child="$child" -v pb="$pb" 'BEGIN {
		midpoint=(pa+pb)/2
		printf "%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\n", \
			analysis, pa, child, pb, midpoint, 100*(child/midpoint-1), 100*(pb/pa-1)
	}' >> "$BASE/sensitivity.tsv"
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

cat "$BASE/summary.tsv"
cat "$BASE/sensitivity.tsv"
cat "$BASE/component-summary.tsv"
echo "analysis_complete=yes"
