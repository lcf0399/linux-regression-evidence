#!/usr/bin/env bash
set -euo pipefail
set -o noclobber

usage() {
	echo "usage: $0 --profile plain|inject|formal|v7-semantic --phase smoke|trace|semantic|point --out-dir DIR [--result-stem NAME]" >&2
}

profile=''; phase=''; out_dir=''; result_stem=''
while (($#)); do
	case "$1" in
	--profile) profile=${2-}; shift 2 ;;
	--phase) phase=${2-}; shift 2 ;;
	--out-dir) out_dir=${2-}; shift 2 ;;
	--result-stem) result_stem=${2-}; shift 2 ;;
	*) usage; exit 2 ;;
	esac
done
[[ "$profile" =~ ^(plain|inject|formal|v7-semantic)$ && "$phase" =~ ^(smoke|trace|semantic|point)$ && -n "$out_dir" ]] || { usage; exit 2; }
[[ "$phase:$profile" =~ ^(smoke:(plain|inject)|trace:(plain|inject)|semantic:v7-semantic|point:formal)$ ]] || { echo "invalid profile/phase pair" >&2; exit 2; }
[[ -z "$result_stem" || "$result_stem" =~ ^[A-Za-z0-9._-]+$ ]] || exit 2
[[ ${NOP_EXECUTE_ACK:-} == YES ]] || { echo "set NOP_EXECUTE_ACK=YES" >&2; exit 1; }

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
binary=${NOP_BINARY:-"$script_dir/build/io_uring_nop_round"}
[[ -x "$binary" ]] || { echo "missing executable: $binary" >&2; exit 1; }

read_actual_preempt() {
	local path=/sys/kernel/debug/sched/preempt text
	if [[ -r "$path" ]]; then text=$(<"$path")
	elif sudo -n test -r "$path" 2>/dev/null; then text=$(sudo -n cat "$path")
	else return 1
	fi
	sed -n 's/.*(\([^)]*\)).*/\1/p' <<<"$text"
}

read_build_preempt() {
	local release=$1 config
	config=$(cat "/boot/config-$release")
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
build_preempt=$(read_build_preempt "$release")
actual_preempt=$(read_actual_preempt || true)
if [[ "$phase" == point ]]; then
	[[ ${NOP_TIMING_ACK:-} == YES ]] || { echo "set NOP_TIMING_ACK=YES" >&2; exit 1; }
	[[ "$requested_preempt" == "${NOP_REQUIRED_REQUESTED_PREEMPT:?}" ]] || { echo "requested preempt mismatch" >&2; exit 1; }
	[[ "$build_preempt" == "${NOP_REQUIRED_BUILD_PREEMPT:?}" ]] || { echo "build preempt mismatch" >&2; exit 1; }
	[[ "$actual_preempt" == "${NOP_REQUIRED_ACTUAL_PREEMPT:?}" ]] || { echo "actual preempt mismatch" >&2; exit 1; }
	[[ $(</sys/devices/system/cpu/cpu2/cpufreq/scaling_governor) == performance ]]
	[[ $(</sys/devices/system/cpu/cpu2/cpufreq/energy_performance_preference) == performance ]]
	[[ $(</sys/devices/system/cpu/intel_pstate/no_turbo) == 1 ]]
fi

mkdir -p -- "$out_dir"
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
boot_id=$(tr -d '\r\n' </proc/sys/kernel/random/boot_id)
stem=${result_stem:-"nop-${profile}-${phase}-${timestamp}-${boot_id}"}
result="$out_dir/$stem.tsv"
environment="$out_dir/$stem.env"
validation="$out_dir/$stem.validation.json"
{
	echo "timestamp_utc=$timestamp"
	echo "profile=$profile"
	echo "phase=$phase"
	echo "binary_sha256=$(sha256sum "$binary" | awk '{print $1}')"
	echo "source_sha256=$(sha256sum "$script_dir/io_uring_nop_round.c" | awk '{print $1}')"
	echo "uname=$(uname -a)"
	echo "boot_id=$boot_id"
	echo "cmdline=$(tr -d '\r\n' </proc/cmdline)"
	echo "preempt_requested=${requested_preempt:-unknown}"
	echo "preempt_build=$build_preempt"
	echo "preempt_actual=${actual_preempt:-unknown}"
	echo "cpu2_governor=$(cat /sys/devices/system/cpu/cpu2/cpufreq/scaling_governor 2>/dev/null || echo unavailable)"
	echo "cpu2_epp=$(cat /sys/devices/system/cpu/cpu2/cpufreq/energy_performance_preference 2>/dev/null || echo unavailable)"
	echo "intel_pstate_no_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo 2>/dev/null || echo unavailable)"
} >"$environment"

"$binary" --profile "$profile" --mode "$phase" --execute --output "$result"
case "$phase" in
point) validator_phase=measured ;;
trace) validator_phase=direct-hit-trace ;;
semantic) validator_phase=semantic-v7 ;;
*) validator_phase=semantic-smoke ;;
esac
python3 "$script_dir/validate_nop_tsv.py" --input "$result" --profile "$profile" --phase "$validator_phase" --output "$validation"
echo "result=$result"
echo "environment=$environment"
echo "validation=$validation"
