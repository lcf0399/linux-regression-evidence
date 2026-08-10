#!/usr/bin/env bash
set -euo pipefail
set -o noclobber

usage()
{
	echo "Usage: $0 --profile r0_remap|l0_standard_lifecycle|n0_userbacked_lifecycle|a0_anon_map|e0_errors --phase describe|smoke|trace|point [--out-dir DIR] [--result-stem NAME]" >&2
}

profile=
phase=
out_dir=
result_stem=
while (($#)); do
	case "$1" in
	--profile) profile=${2-}; shift 2 ;;
	--phase) phase=${2-}; shift 2 ;;
	--out-dir) out_dir=${2-}; shift 2 ;;
	--result-stem) result_stem=${2-}; shift 2 ;;
	*) usage; exit 2 ;;
	esac
done
[[ "$profile" =~ ^(r0_remap|l0_standard_lifecycle|n0_userbacked_lifecycle|a0_anon_map|e0_errors)$ ]] || { usage; exit 2; }
[[ "$phase" =~ ^(describe|smoke|trace|point)$ ]] || { usage; exit 2; }
[[ "$profile" != e0_errors || "$phase" =~ ^(describe|smoke)$ ]] || exit 2
[[ -z "$result_stem" || "$result_stem" =~ ^[A-Za-z0-9._-]+$ ]] || exit 2

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
binary=${MEMMAP_BINARY:-"$script_dir/build/io_uring_memmap_round"}
[[ -x "$binary" ]] || { echo "Missing executable: $binary" >&2; exit 1; }
if [[ "$phase" == describe ]]; then
	exec "$binary" --profile "$profile" --mode describe
fi
[[ -n "$out_dir" ]] || { usage; exit 2; }
[[ ${MEMMAP_EXECUTE_ACK:-} == YES ]] || {
	echo "Execution is locked; set MEMMAP_EXECUTE_ACK=YES." >&2; exit 1;
}
if [[ "$phase" == point ]]; then
	[[ ${MEMMAP_TIMING_ACK:-} == YES ]] || {
		echo "Formal timing requires MEMMAP_TIMING_ACK=YES." >&2; exit 1;
	}
	: "${MEMMAP_REQUIRED_REQUESTED_PREEMPT:?set requested preempt contract}"
	: "${MEMMAP_REQUIRED_BUILD_PREEMPT:?set build preempt contract}"
	: "${MEMMAP_REQUIRED_ACTUAL_PREEMPT:?set actual preempt contract}"
fi

read_actual_preempt()
{
	local path=/sys/kernel/debug/sched/preempt text
	if [[ -r "$path" ]]; then text=$(<"$path")
	elif sudo -n test -r "$path" 2>/dev/null; then text=$(sudo -n cat "$path")
	else return 1
	fi
	sed -n 's/.*(\([^)]*\)).*/\1/p' <<<"$text"
}

read_build_config()
{
	local release=$1
	if [[ -r /proc/config.gz ]]; then zcat /proc/config.gz
	elif [[ -r "/boot/config-$release" ]]; then cat "/boot/config-$release"
	else return 1
	fi
}

resolve_build_preempt()
{
	local config=$1
	if grep -qx 'CONFIG_PREEMPT_DYNAMIC=y' <<<"$config"; then echo dynamic
	elif grep -qx 'CONFIG_PREEMPT_LAZY=y' <<<"$config"; then echo lazy
	elif grep -qx 'CONFIG_PREEMPT=y' <<<"$config"; then echo full
	elif grep -qx 'CONFIG_PREEMPT_VOLUNTARY=y' <<<"$config"; then echo voluntary
	elif grep -qx 'CONFIG_PREEMPT_NONE=y' <<<"$config"; then echo none
	else echo unknown
	fi
}

release=$(uname -r)
requested_preempt=$(tr ' ' '\n' </proc/cmdline | sed -n 's/^preempt=//p')
build_config=$(read_build_config "$release" || true)
build_preempt=$(resolve_build_preempt "$build_config")
actual_preempt=$(read_actual_preempt || true)
if [[ "$phase" == point ]]; then
	[[ "$requested_preempt" == "$MEMMAP_REQUIRED_REQUESTED_PREEMPT" ]] || {
		echo "requested preempt mismatch: ${requested_preempt:-unknown}" >&2; exit 1;
	}
	[[ "$build_preempt" == "$MEMMAP_REQUIRED_BUILD_PREEMPT" ]] || {
		echo "build preempt mismatch: $build_preempt" >&2; exit 1;
	}
	[[ "$actual_preempt" == "$MEMMAP_REQUIRED_ACTUAL_PREEMPT" ]] || {
		echo "actual preempt mismatch: ${actual_preempt:-unknown}" >&2; exit 1;
	}
	[[ $(</sys/devices/system/cpu/cpu2/cpufreq/scaling_governor) == performance ]]
	[[ $(</sys/devices/system/cpu/cpu2/cpufreq/energy_performance_preference) == performance ]]
	[[ $(</sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]]
	[[ $(sudo -n cat /sys/kernel/debug/tracing/tracing_on) == 0 ]]
fi

mkdir -p -- "$out_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
boot_id=$(tr -d '\r\n' </proc/sys/kernel/random/boot_id)
stem=${result_stem:-"memmap-${profile}-${phase}-${timestamp}-${boot_id}"}
result="$out_dir/$stem.tsv"
environment="$out_dir/$stem.env"
validation="$out_dir/$stem.validation.json"
trace_enabled=$(sudo -n cat /sys/kernel/debug/tracing/tracing_on)
[[ "$trace_enabled" =~ ^[01]$ ]] || {
	echo "unable to determine tracing_on state" >&2; exit 1;
}

{
	echo "timestamp_utc=$timestamp"
	echo "profile=$profile"
	echo "phase=$phase"
	echo "binary=$binary"
	echo "binary_sha256=$(sha256sum "$binary" | awk '{print $1}')"
	echo "source_sha256=$(sha256sum "$script_dir/io_uring_memmap_round.c" | awk '{print $1}')"
	echo "contract_sha256=$(sha256sum "$script_dir/memmap-contract.json" | awk '{print $1}')"
	echo "uname=$(uname -a)"
	echo "boot_id=$boot_id"
	echo "cmdline=$(tr -d '\r\n' </proc/cmdline)"
	echo "preempt_requested=${requested_preempt:-unknown}"
	echo "preempt_build=$build_preempt"
	echo "preempt_actual=${actual_preempt:-unknown}"
	echo "preempt_required_requested=${MEMMAP_REQUIRED_REQUESTED_PREEMPT:-not-applicable}"
	echo "preempt_required_build=${MEMMAP_REQUIRED_BUILD_PREEMPT:-not-applicable}"
	echo "preempt_required_actual=${MEMMAP_REQUIRED_ACTUAL_PREEMPT:-not-applicable}"
	echo "cpu2_governor=$(cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
	echo "cpu2_epp=$(cat /sys/devices/system/cpu/cpu2/cpufreq/energy_performance_preference 2>/dev/null || echo unavailable)"
	echo "intel_pstate_no_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo unavailable)"
} >"$environment"

MEMMAP_TRACE_ENABLED=$trace_enabled \
	"$binary" --profile "$profile" --mode "$phase" --cpu 2 \
	--output "$result" --execute
validator_phase=semantic-smoke
[[ "$phase" == trace ]] && validator_phase=direct-hit
[[ "$phase" == point ]] && validator_phase=measured
python3 "$script_dir/validate_memmap_tsv.py" \
	--input "$result" --profile "$profile" --phase "$validator_phase" \
	--output "$validation"

echo "result=$result"
echo "environment=$environment"
echo "validation=$validation"
