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
HELPERS=${HELPERS:-$ROOT/scripts/baremetal/kernel}
SOURCE=${SOURCE:-$ROOT/linux-regression-evidence/mprotect-shared-dirty-toggle/bare-metal/20260722-v713-shared-pte-hint-fastpath-ab/mprotect_shared_pte_mapped_thp_reproducer.c}
MANIFEST="$BASE/build-logs/$ROLE.artifacts.tsv"
WARMUP_RUNS=${WARMUP_RUNS:-2}
MEASURED_RUNS=${MEASURED_RUNS:-15}
PIN_CPU=${PIN_CPU:-2}
ITERATIONS=${ITERATIONS:-200}
INTERNAL_WARMUP=${INTERNAL_WARMUP:-5}
BOOT_SETTLE_SECONDS=${BOOT_SETTLE_SECONDS:-60}
SHMEM_CONTROL=/sys/kernel/mm/transparent_hugepage/shmem_enabled

fail()
{
	echo "large-folio point failed: $*" >&2
	exit 1
}

manifest_value()
{
	local key=$1
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$MANIFEST"
}

result_value()
{
	local line=$1
	local key=$2
	printf '%s\n' "$line" | tr ' ' '\n' |
		awk -F= -v key="$key" '$1 == key { print $2; exit }'
}

EXPECTED_RELEASE=$(manifest_value kernelrelease)
[[ -n "$EXPECTED_RELEASE" ]] || fail "missing kernelrelease in $MANIFEST"
[[ $(uname -r) == "$EXPECTED_RELEASE" ]] ||
	fail "running $(uname -r), expected $EXPECTED_RELEASE"
case " $(cat /proc/cmdline) " in
	*' preempt=none '*) ;;
	*) fail "running cmdline lacks preempt=none" ;;
esac
[[ -r "$SHMEM_CONTROL" ]] || fail "missing $SHMEM_CONTROL"

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
RUN_DIR="$BASE/large-folio-runs/${STAMP}_${POINT_LABEL}_${EXPECTED_RELEASE}"
mkdir -p "$RUN_DIR" "$BASE/bin"
exec > >(tee "$RUN_DIR/point.log") 2>&1

echo "waiting ${BOOT_SETTLE_SECONDS}s after boot"
sleep "$BOOT_SETTLE_SECONDS"
WORK="$BASE" \
	ARTIFACT_MANIFEST_OVERRIDE="$MANIFEST" \
	KERNEL_RELEASE_OVERRIDE="$EXPECTED_RELEASE" \
	MANIFEST_ID_KEY=role \
	MANIFEST_ID_VALUE="$ROLE" \
	"$HELPERS/record_stable_kernel_boot_smoke.sh" "$ROLE" "$BASE/large-folio-boot-smoke"
"$HELPERS/cleanup_stable_kernel_efi_bootnext.sh"

PROFILE_BEFORE="$RUN_DIR/cpu-profile-before.tsv"
PROFILE_APPLIED="$RUN_DIR/cpu-profile-applied.tsv"
PROFILE_AFTER="$RUN_DIR/cpu-profile-after.tsv"
SHMEM_BEFORE=$(<"$SHMEM_CONTROL")
SHMEM_SELECTED=$(printf '%s\n' "$SHMEM_BEFORE" | sed -n 's/.*\[\([^]]*\)\].*/\1/p')
[[ -n "$SHMEM_SELECTED" ]] || fail "cannot parse selected shmem THP mode"

snapshot_cpu_profile()
{
	local output=$1
	: > "$output"
	for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor \
		/sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do
		[[ -r "$path" ]] && printf '%s\t%s\n' "$path" "$(<"$path")" >> "$output"
	done
	if [[ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
		printf '%s\t%s\n' /sys/devices/system/cpu/intel_pstate/no_turbo \
			"$(</sys/devices/system/cpu/intel_pstate/no_turbo)" >> "$output"
	fi
}

apply_cpu_profile()
{
	for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
		[[ -e "$path" ]] && echo performance | sudo tee "$path" >/dev/null
	done
	for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do
		[[ -e "$path" ]] && echo performance | sudo tee "$path" >/dev/null
	done
	if [[ -e /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
		echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo >/dev/null
	fi
}

restore_environment()
{
	printf '%s\n' "$SHMEM_SELECTED" | sudo tee "$SHMEM_CONTROL" >/dev/null || true
	while IFS=$'\t' read -r path value; do
		[[ -n "$path" && -e "$path" ]] || continue
		printf '%s\n' "$value" | sudo tee "$path" >/dev/null || true
	done < "$PROFILE_BEFORE"
}

trap 'restore_environment' EXIT
snapshot_cpu_profile "$PROFILE_BEFORE"
apply_cpu_profile
printf '%s\n' advise | sudo tee "$SHMEM_CONTROL" >/dev/null
snapshot_cpu_profile "$PROFILE_APPLIED"
SHMEM_APPLIED=$(<"$SHMEM_CONTROL")
printf '%s\n' "$SHMEM_BEFORE" > "$RUN_DIR/shmem-enabled-before.txt"
printf '%s\n' "$SHMEM_APPLIED" > "$RUN_DIR/shmem-enabled-applied.txt"

for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
	[[ ! -r "$path" || $(<"$path") == performance ]] || fail "$path is not performance"
done
for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do
	[[ ! -r "$path" || $(<"$path") == performance ]] || fail "$path is not performance"
done
[[ ! -r /sys/devices/system/cpu/intel_pstate/no_turbo ||
	$(</sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || fail "Turbo is enabled"
grep -Fq '[advise]' "$SHMEM_CONTROL" || fail "shmem THP advise mode was not selected"

[[ -s "$SOURCE" ]] || fail "missing reproducer source $SOURCE"
BIN="$BASE/bin/mprotect_shared_pte_mapped_thp_reproducer"
gcc -O2 -Wall -Wextra -Werror -o "$BIN" "$SOURCE"

cat > "$RUN_DIR/env.txt" <<EOF
timestamp_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
role=$ROLE
point_label=$POINT_LABEL
commit=$(manifest_value commit)
kernel_release=$(uname -r)
boot_id=$(cat /proc/sys/kernel/random/boot_id)
kernel_cmdline=$(cat /proc/cmdline)
pin_cpu=$PIN_CPU
warmup_runs=$WARMUP_RUNS
measured_runs=$MEASURED_RUNS
iterations=$ITERATIONS
internal_warmup=$INTERNAL_WARMUP
reproducer_source_sha256=$(sha256sum "$SOURCE" | awk '{ print $1 }')
reproducer_binary_sha256=$(sha256sum "$BIN" | awk '{ print $1 }')
shmem_enabled_before=$SHMEM_BEFORE
shmem_enabled_applied=$SHMEM_APPLIED
failed_units=$(systemctl --failed --no-legend --plain | awk 'NF { n++ } END { print n + 0 }')
EOF

for warmup in $(seq 1 "$WARMUP_RUNS"); do
	echo "large_folio_warmup=$warmup/$WARMUP_RUNS"
	set +e
	# shellcheck disable=SC2024 # RUN_DIR is user-owned; only the workload needs root.
	sudo env ITERATIONS="$ITERATIONS" WARMUP="$INTERNAL_WARMUP" \
		taskset -c "$PIN_CPU" "$BIN" > "$RUN_DIR/warmup-$warmup.log" 2>&1
	rc=$?
	set -e
	if (( rc == 4 )); then
		printf 'availability\tunavailable\n' > "$RUN_DIR/availability.tsv"
		printf 'reason\t%s\n' "$(tr '\n' ' ' < "$RUN_DIR/warmup-$warmup.log")" \
			>> "$RUN_DIR/availability.tsv"
		cat "$RUN_DIR/availability.tsv"
		exit 4
	fi
	(( rc == 0 )) || fail "large-folio warmup failed with rc=$rc"
done

printf 'round\tprotect_ns_per_page\trestore_ns_per_page\ttouch_ns_per_page\titeration_ns_per_page\texpected_match_ratio\tunexpected_results\tcollapsed_large_kb\tsplit_compound_head\tsplit_compound_tail\tsplit_thp\tafter_thp\n' \
	> "$RUN_DIR/measurements.tsv"
: > "$RUN_DIR/raw.log"

for round in $(seq 1 "$MEASURED_RUNS"); do
	echo "large_folio_measured=$round/$MEASURED_RUNS"
	output=$(sudo env ITERATIONS="$ITERATIONS" WARMUP="$INTERNAL_WARMUP" \
		taskset -c "$PIN_CPU" "$BIN")
	printf 'round=%s\n%s\n' "$round" "$output" >> "$RUN_DIR/raw.log"
	result=$(printf '%s\n' "$output" | awk '/^result / { line=$0 } END { print line }')
	[[ -n "$result" ]] || fail "round $round produced no result"
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$round" \
		"$(result_value "$result" protect_ns_per_page)" \
		"$(result_value "$result" restore_ns_per_page)" \
		"$(result_value "$result" touch_ns_per_page)" \
		"$(result_value "$result" iteration_ns_per_page)" \
		"$(result_value "$result" expected_match_ratio)" \
		"$(result_value "$result" unexpected_results)" \
		"$(result_value "$result" collapsed_large_kb)" \
		"$(result_value "$result" split_compound_head)" \
		"$(result_value "$result" split_compound_tail)" \
		"$(result_value "$result" split_thp)" \
		"$(result_value "$result" after_thp)" >> "$RUN_DIR/measurements.tsv"
done

awk -F '\t' '
	NR == 1 { next }
	{
		n++; x=($2+0)+($3+0); sum+=x; sumsq+=x*x;
		protect+=$2; restore+=$3; touch+=$4; iteration+=$5;
		values=(values == "" ? x : values " " x)
		if ($6 != 100 || $7 != 0 || $8 < 2048 || $9 < 1 ||
		    $10 < 1 || $11 < 1 || $12 < 1) bad++
	}
	END {
		mean=sum/n; variance=(n > 1 ? (sumsq-sum*sum/n)/(n-1) : 0)
		if (variance < 0) variance=0
		sd=sqrt(variance); cv=(mean ? 100*sd/mean : 0)
		print "role\tpoint_label\tkernel\tn\tmean_mprotect_ns_per_page\tmean_protect_ns_per_page\tmean_restore_ns_per_page\tmean_touch_ns_per_page\tmean_iteration_ns_per_page\tsd_mprotect\tcv_mprotect_pct\tvalues_mprotect\tsemantic_failures"
		printf "%s\t%s\t%s\t%d\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%s\t%d\n", role, label, kernel, n, mean, protect/n, restore/n, touch/n, iteration/n, sd, cv, values, bad+0
	}
' role="$ROLE" label="$POINT_LABEL" kernel="$(uname -r)" \
	"$RUN_DIR/measurements.tsv" > "$RUN_DIR/summary.tsv"
[[ $(awk -F '\t' 'NR == 2 { print $13 }' "$RUN_DIR/summary.tsv") == 0 ]] ||
	fail "one or more large-folio rounds failed shape/semantic checks"

printf 'availability\tavailable\n' > "$RUN_DIR/availability.tsv"
restore_environment
trap - EXIT
snapshot_cpu_profile "$PROFILE_AFTER"
printf '%s\n' "$(<"$SHMEM_CONTROL")" > "$RUN_DIR/shmem-enabled-after.txt"
[[ $(<"$SHMEM_CONTROL") == "$SHMEM_BEFORE" ]] || fail "shmem THP mode was not restored"

cat "$RUN_DIR/summary.tsv"
echo "run_dir=$RUN_DIR"
