#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
SRC="$SCRIPT_DIR/remap_range_brd_micro.c"
BUILD_DIR=${BUILD_DIR:-"$SCRIPT_DIR/out/build"}
BIN="$BUILD_DIR/remap_range_brd_micro"
OUT_BASE=${OUT_BASE:-"$SCRIPT_DIR/out/results"}
PIN_CPU=${PIN_CPU:-2}
EXTERNAL_ROUNDS=${EXTERNAL_ROUNDS:-5}
ITERATIONS=${ITERATIONS:-10000}
RANGE_BYTES=${RANGE_BYTES:-4096}
BRD_DEV=${BRD_DEV:-/dev/ram0}
BRD_SIZE_MB=${BRD_SIZE_MB:-1024}
FSTYPES=${FSTYPES:-"btrfs"}

for value in "$PIN_CPU" "$EXTERNAL_ROUNDS" "$ITERATIONS" "$RANGE_BYTES" "$BRD_SIZE_MB"; do
	if ! [[ "$value" =~ ^[0-9]+$ ]] || (( value == 0 )); then
		echo "expected a positive integer, got: $value" >&2
		exit 2
	fi
done
if (( RANGE_BYTES % 4096 != 0 )); then
	echo "RANGE_BYTES must be a multiple of 4096: $RANGE_BYTES" >&2
	exit 2
fi

if [[ "$BRD_DEV" != /dev/ram* ]]; then
	echo "refusing non-brd device: $BRD_DEV" >&2
	exit 2
fi

command -v sudo >/dev/null
sudo -n true
command -v cc >/dev/null
command -v mkfs.btrfs >/dev/null
command -v mount >/dev/null
command -v umount >/dev/null

mkdir -p "$BUILD_DIR" "$OUT_BASE"
cc -O2 -Wall -Wextra -std=gnu11 "$SRC" -o "$BIN"

kernel=$(uname -r)
stamp=$(date -u +%Y%m%dT%H%M%SZ)
run_dir="$OUT_BASE/${stamp}_${kernel}"
mkdir -p "$run_dir"

work_dir=$(mktemp -d /tmp/remap-range-brd.XXXXXX)
mnt="$work_dir/mnt"
mkdir -p "$mnt"

# shellcheck disable=SC2317 # cleanup is invoked indirectly by trap
cleanup()
{
	set +e
	if mountpoint -q "$mnt"; then
		sudo -n umount "$mnt"
	fi
	rm -rf "$work_dir"
}
trap cleanup EXIT

sudo -n modprobe brd rd_nr=1 rd_size=$((BRD_SIZE_MB * 1024)) 2>/dev/null || true
if [[ ! -b "$BRD_DEV" ]]; then
	echo "missing brd device: $BRD_DEV" >&2
	exit 2
fi
if findmnt -rn -S "$BRD_DEV" >/dev/null 2>&1; then
	echo "$BRD_DEV is already mounted; refusing to overwrite it" >&2
	exit 2
fi

brd_bytes=$(sudo -n blockdev --getsize64 "$BRD_DEV" 2>/dev/null || echo 0)
required_bytes=$((2 * ITERATIONS * RANGE_BYTES + 128 * 1024 * 1024))
if (( brd_bytes < required_bytes )); then
	echo "brd device is too small: bytes=$brd_bytes required_at_least=$required_bytes" >&2
	exit 2
fi
governor_file="/sys/devices/system/cpu/cpu${PIN_CPU}/cpufreq/scaling_governor"
driver_file="/sys/devices/system/cpu/cpu${PIN_CPU}/cpufreq/scaling_driver"
epp_file="/sys/devices/system/cpu/cpu${PIN_CPU}/cpufreq/energy_performance_preference"
cpu_governor=unavailable
cpu_driver=unavailable
cpu_epp=unavailable
[[ -r "$governor_file" ]] && cpu_governor=$(<"$governor_file")
[[ -r "$driver_file" ]] && cpu_driver=$(<"$driver_file")
[[ -r "$epp_file" ]] && cpu_epp=$(<"$epp_file")
{
	echo "kernel=$kernel"
	echo "timestamp_utc=$stamp"
	echo "pin_cpu=$PIN_CPU"
	echo "external_rounds=$EXTERNAL_ROUNDS"
	echo "iterations=$ITERATIONS"
	echo "range_bytes=$RANGE_BYTES"
	echo "bytes_per_file=$((ITERATIONS * RANGE_BYTES))"
	echo "brd_dev=$BRD_DEV"
	echo "brd_bytes=$brd_bytes"
	echo "brd_size_mb_requested=$BRD_SIZE_MB"
	echo "fstypes=$FSTYPES"
	echo "cpu_governor=$cpu_governor"
	echo "cpu_driver=$cpu_driver"
	echo "cpu_epp=$cpu_epp"
	echo "binary=$BIN"
	echo "compiler=$(cc --version | head -n 1)"
	echo "mkfs_btrfs=$(mkfs.btrfs --version 2>&1 | head -n 1)"
	echo "mkfs_xfs=$(command -v mkfs.xfs >/dev/null && mkfs.xfs -V 2>&1 | head -n 1 || echo missing)"
} > "$run_dir/run_env.txt"

summary="$run_dir/summary.tsv"
printf 'fstype\tround\tscenario\tops\tbytes\telapsed_ns\tns_per_op\tchecksum\texpected_match_ratio\tunexpected_results\n' > "$summary"

run_one_fs()
{
	local fstype=$1
	local mkfs_cmd

	case "$fstype" in
	btrfs)
		mkfs_cmd=(sudo -n mkfs.btrfs -q -f "$BRD_DEV")
		;;
	xfs)
		if ! command -v mkfs.xfs >/dev/null 2>&1; then
			echo "skip xfs: mkfs.xfs missing" | tee "$run_dir/xfs.skip"
			return 0
		fi
		mkfs_cmd=(sudo -n mkfs.xfs -q -f -m reflink=1 "$BRD_DEV")
		;;
	*)
		echo "unknown fstype: $fstype" >&2
		exit 2
		;;
	esac

	"${mkfs_cmd[@]}" > "$run_dir/${fstype}.mkfs.log" 2>&1
	sudo -n mount -t "$fstype" -o noatime "$BRD_DEV" "$mnt"
	sudo -n chmod 777 "$mnt"

	for round in $(seq 1 "$EXTERNAL_ROUNDS"); do
		local log="$run_dir/${fstype}-round-${round}.log"
		local cmd=("$BIN" "$mnt" "$ITERATIONS" "$RANGE_BYTES")
		if command -v taskset >/dev/null 2>&1; then
			cmd=(taskset -c "$PIN_CPU" "${cmd[@]}")
		fi
		"${cmd[@]}" > "$log"
		awk -v round="$round" -v fstype="$fstype" '
			/^scenario=/ {
				scenario=ops=bytes=elapsed=nsop=checksum=expected=unexpected="";
				for (i = 1; i <= NF; i++) {
					split($i, kv, "=");
					if (kv[1] == "scenario") scenario=kv[2];
					if (kv[1] == "ops") ops=kv[2];
					if (kv[1] == "bytes") bytes=kv[2];
					if (kv[1] == "elapsed_ns") elapsed=kv[2];
					if (kv[1] == "ns_per_op") nsop=kv[2];
					if (kv[1] == "checksum") checksum=kv[2];
					if (kv[1] == "expected_match_ratio") expected=kv[2];
					if (kv[1] == "unexpected_results") unexpected=kv[2];
				}
				printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", fstype, round, scenario, ops, bytes, elapsed, nsop, checksum, expected, unexpected;
			}
		' "$log" >> "$summary"
	done

	sudo -n umount "$mnt"
}

for fstype in $FSTYPES; do
	run_one_fs "$fstype"
done

awk -F'\t' '
	NR > 1 {
		key=$1 "\t" $3;
		n[key]++;
		sum[key]+=$7;
		if (!(key in min) || $7 < min[key]) min[key]=$7;
		if (!(key in max) || $7 > max[key]) max[key]=$7;
		unexpected += $10;
		if (NR == 2 || $9 < min_expected) min_expected=$9;
	}
	END {
		print "fstype\tscenario\trounds\tmean_ns_per_op\tmin_ns_per_op\tmax_ns_per_op";
		for (key in n)
			printf "%s\t%d\t%.3f\t%.3f\t%.3f\n", key, n[key], sum[key]/n[key], min[key], max[key];
		printf "unexpected_results_total\t%d\n", unexpected > "/dev/stderr";
		printf "min_expected_match_ratio\t%d\n", min_expected > "/dev/stderr";
	}
' "$summary" > "$run_dir/summary_by_scenario.tsv" 2> "$run_dir/semantic_status.txt"

unexpected_sum=$(awk -F'\t' 'NR > 1 {sum += $10} END {print sum+0}' "$summary")
min_expected=$(awk -F'\t' 'NR == 2 {min=$9} NR > 2 && $9 < min {min=$9} END {print min+0}' "$summary")
exit_code=0
if [[ "$unexpected_sum" != 0 || "$min_expected" != 100 ]]; then
	exit_code=1
fi

echo "run_dir=$run_dir"
echo "exit_code=$exit_code"
echo "expected_match_ratio=$min_expected"
echo "unexpected_results=$unexpected_sum"
exit "$exit_code"
