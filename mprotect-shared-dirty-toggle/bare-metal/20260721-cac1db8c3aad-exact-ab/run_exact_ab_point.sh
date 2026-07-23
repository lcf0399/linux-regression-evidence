#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

if [[ $# -ne 2 ]]; then
	echo "usage: $0 <manifest-role> <point-label>" >&2
	exit 2
fi

ROLE=$1
POINT_LABEL=$2
[[ "$ROLE" =~ ^[a-z][a-z0-9-]*$ ]] || {
	echo "invalid manifest role: $ROLE" >&2
	exit 2
}

ROOT=${ROOT:-/home/lcf/kernel-study}
BASE=${BASE:-$ROOT/linux-baremetal/mprotect-cac1-exact-20260721}
HELPERS=${HELPERS:-$ROOT/scripts/baremetal/kernel}
REPRODUCER_SOURCE=${REPRODUCER_SOURCE:-$BASE/workload/mprotect_shared_dirty_reproducer.c}
MANIFEST="$BASE/build-logs/$ROLE.artifacts.tsv"
WARMUP_RUNS=${WARMUP_RUNS:-3}
MEASURED_RUNS=${MEASURED_RUNS:-15}
PIN_CPU=${PIN_CPU:-2}
MAPPING_MB=${MAPPING_MB:-64}
ITERATIONS=${ITERATIONS:-1000}
INTERNAL_WARMUP=${INTERNAL_WARMUP:-10}
BOOT_SETTLE_SECONDS=${BOOT_SETTLE_SECONDS:-60}

fail()
{
	echo "exact A/B point failed: $*" >&2
	exit 1
}

manifest_value()
{
	local key=$1
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$MANIFEST"
}

EXPECTED_RELEASE=$(manifest_value kernelrelease)
[[ -n "$EXPECTED_RELEASE" ]] || fail "missing kernelrelease in $MANIFEST"
[[ $(uname -r) == "$EXPECTED_RELEASE" ]] ||
	fail "running $(uname -r), expected $EXPECTED_RELEASE"
case " $(cat /proc/cmdline) " in
	*' preempt=none '*) ;;
	*) fail "running cmdline lacks preempt=none" ;;
esac

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
RUN_DIR="$BASE/runs/${STAMP}_${POINT_LABEL}_${EXPECTED_RELEASE}"
mkdir -p "$RUN_DIR" "$BASE/bin"
exec > >(tee "$RUN_DIR/point.log") 2>&1

echo "waiting ${BOOT_SETTLE_SECONDS}s after boot"
sleep "$BOOT_SETTLE_SECONDS"

# The one-shot entry has already been consumed. Record the accepted boot before
# deleting the temporary EFI entry and staged files.
WORK="$BASE" \
	ARTIFACT_MANIFEST_OVERRIDE="$MANIFEST" \
	KERNEL_RELEASE_OVERRIDE="$EXPECTED_RELEASE" \
	MANIFEST_ID_KEY=role \
	MANIFEST_ID_VALUE="$ROLE" \
	"$HELPERS/record_stable_kernel_boot_smoke.sh" "$ROLE" "$BASE/boot-smoke"
"$HELPERS/cleanup_stable_kernel_efi_bootnext.sh"

PROFILE_BEFORE="$RUN_DIR/cpu-profile-before.tsv"
PROFILE_APPLIED="$RUN_DIR/cpu-profile-applied.tsv"
PROFILE_AFTER="$RUN_DIR/cpu-profile-after.tsv"

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

restore_cpu_profile()
{
	while IFS=$'\t' read -r path value; do
		[[ -n "$path" && -e "$path" ]] || continue
		printf '%s\n' "$value" | sudo tee "$path" >/dev/null
	done < "$PROFILE_BEFORE"
}

trap 'restore_cpu_profile || true' EXIT
snapshot_cpu_profile "$PROFILE_BEFORE"
apply_cpu_profile
snapshot_cpu_profile "$PROFILE_APPLIED"

for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_governor; do
	[[ ! -r "$path" || $(<"$path") == performance ]] || fail "$path is not performance"
done
for path in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/energy_performance_preference; do
	[[ ! -r "$path" || $(<"$path") == performance ]] || fail "$path is not performance"
done
[[ ! -r /sys/devices/system/cpu/intel_pstate/no_turbo ||
	$(</sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]] || fail "Turbo is still enabled"

sleep 10

[[ -s "$REPRODUCER_SOURCE" ]] || fail "missing reproducer source $REPRODUCER_SOURCE"
BIN="$BASE/bin/mprotect_shared_dirty_reproducer"
gcc -O2 -Wall -Wextra -o "$BIN" "$REPRODUCER_SOURCE"

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
mapping_mb=$MAPPING_MB
iterations_per_process=$ITERATIONS
internal_warmup=$INTERNAL_WARMUP
reproducer_source_sha256=$(sha256sum "$REPRODUCER_SOURCE" | awk '{ print $1 }')
reproducer_binary_sha256=$(sha256sum "$BIN" | awk '{ print $1 }')
config_sha256=$(sha256sum "/boot/config-$(uname -r)" | awk '{ print $1 }')
bzimage_sha256=$(sha256sum "/boot/vmlinuz-$(uname -r)" | awk '{ print $1 }')
compiler=$(gcc --version | head -n1)
system_state=$(systemctl is-system-running || true)
failed_units=$(systemctl --failed --no-legend --plain | awk 'NF { n++ } END { print n + 0 }')
EOF

for warmup in $(seq 1 "$WARMUP_RUNS"); do
	echo "warmup=$warmup/$WARMUP_RUNS"
	taskset -c "$PIN_CPU" "$BIN" shared_dirty_full_toggle_64m 1 \
		--mapping-mb "$MAPPING_MB" --iterations "$ITERATIONS" \
		--warmup "$INTERNAL_WARMUP" > "$RUN_DIR/warmup-$warmup.log"
done

printf 'round\tprotect_ns_per_page\trestore_ns_per_page\tpost_touch_ns_per_page\titeration_ns_per_page\texpected_match_ratio\tunexpected_results\tkernel_page_kb\tmmu_page_kb\tanon_huge_kb\n' \
	> "$RUN_DIR/measurements.tsv"
: > "$RUN_DIR/raw.log"

for round in $(seq 1 "$MEASURED_RUNS"); do
	echo "measured_round=$round/$MEASURED_RUNS"
	output=$(taskset -c "$PIN_CPU" "$BIN" shared_dirty_full_toggle_64m 1 \
		--mapping-mb "$MAPPING_MB" --iterations "$ITERATIONS" \
		--warmup "$INTERNAL_WARMUP")
	printf 'round=%s\n%s\n' "$round" "$output" >> "$RUN_DIR/raw.log"
	result=$(printf '%s\n' "$output" | awk '/^result / { line=$0 } END { print line }')
	[[ -n "$result" ]] || fail "round $round did not produce a result line"
	get_value()
	{
		printf '%s\n' "$result" | tr ' ' '\n' | awk -F= -v key="$1" '$1 == key { print $2; exit }'
	}
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$round" \
		"$(get_value protect_ns_per_page)" \
		"$(get_value restore_ns_per_page)" \
		"$(get_value post_touch_ns_per_page)" \
		"$(get_value iteration_ns_per_page)" \
		"$(get_value expected_match_ratio)" \
		"$(get_value unexpected_results)" \
		"$(get_value smaps_after_kernel_page_kb)" \
		"$(get_value smaps_after_mmu_page_kb)" \
		"$(get_value smaps_after_anon_huge_kb)" >> "$RUN_DIR/measurements.tsv"
done

awk -F '\t' '
	NR == 1 { next }
	{
		n++
		x=$5+0
		sum+=x
		sumsq+=x*x
		values=(values == "" ? x : values " " x)
		if ($6 != 100 || $7 != 0 || $8 != 4 || $9 != 4 || $10 != 0)
			bad++
	}
	END {
		mean=sum/n
		variance=(n > 1 ? (sumsq - sum*sum/n)/(n-1) : 0)
		if (variance < 0) variance=0
		sd=sqrt(variance)
		cv=(mean != 0 ? 100*sd/mean : 0)
		printf "role\tpoint_label\tkernel\tn\tmean_iteration_ns_per_page\tsd\tcv_pct\tvalues\tsemantic_failures\n"
		printf "%s\t%s\t%s\t%d\t%.6f\t%.6f\t%.6f\t%s\t%d\n", role, label, kernel, n, mean, sd, cv, values, bad+0
	}
' role="$ROLE" label="$POINT_LABEL" kernel="$(uname -r)" \
	"$RUN_DIR/measurements.tsv" > "$RUN_DIR/summary.tsv"

[[ $(awk -F '\t' 'NR == 2 { print $9 }' "$RUN_DIR/summary.tsv") == 0 ]] ||
	fail "one or more measured rounds failed semantic/state checks"

restore_cpu_profile
trap - EXIT
snapshot_cpu_profile "$PROFILE_AFTER"

cat "$RUN_DIR/summary.tsv"
echo "run_dir=$RUN_DIR"
