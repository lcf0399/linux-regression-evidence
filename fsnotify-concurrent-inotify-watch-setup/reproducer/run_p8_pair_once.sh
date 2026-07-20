#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
SOURCE=${SOURCE:-"$SCRIPT_DIR/fsnotify_connector_topology.c"}
OUT_BASE=${OUT_BASE:-"$PWD/fsnotify-p8-results"}
TEST_DIR=${TEST_DIR:-/dev/shm}
ITEMS=${ITEMS:-96}
WORKERS=${WORKERS:-8}
CPU_LIST=${CPU_LIST:-0,2,4,6,8,10,12,14}
WARMUP_ROUNDS=${WARMUP_ROUNDS:-2}
ROUNDS=${ROUNDS:-25}

die()
{
	echo "fsnotify paired runner: $*" >&2
	exit 2
}

for cmd in awk cc date lscpu python3 sha256sum stat uname; do
	command -v "$cmd" >/dev/null || die "missing command: $cmd"
done
[[ -f "$SOURCE" ]] || die "missing source: $SOURCE"
[[ -d "$TEST_DIR" && -w "$TEST_DIR" ]] || die "TEST_DIR is not writable: $TEST_DIR"
for item in "ITEMS:$ITEMS" "WORKERS:$WORKERS" "ROUNDS:$ROUNDS"; do
	name=${item%%:*}
	value=${item#*:}
	if ! [[ "$value" =~ ^[0-9]+$ ]] || (( value == 0 )); then
		die "$name must be positive"
	fi
done
[[ "$WARMUP_ROUNDS" =~ ^[0-9]+$ ]] || die 'WARMUP_ROUNDS must be non-negative'
(( ITEMS % WORKERS == 0 )) || die 'ITEMS must be divisible by WORKERS'
[[ $(awk -F, '{print NF}' <<< "$CPU_LIST") == "$WORKERS" ]] || \
	die 'CPU_LIST must contain exactly WORKERS logical CPUs'

stamp=$(date -u +%Y%m%dT%H%M%SZ)
run_dir="$OUT_BASE/${stamp}_$(uname -r)"
mkdir -p "$run_dir/logs" "$run_dir/build"
binary="$run_dir/build/fsnotify_connector_topology"
cc -std=gnu11 -O2 -g -Wall -Wextra -Werror -pthread "$SOURCE" -o "$binary"

results="$run_dir/results.tsv"
printf 'phase\tround\tposition\ttopology\tworkers\tmetric_name\tmetric_value\tsemantic_status\toverflow_events\tunexpected_results\taffinity_ok\tlog\n' > "$results"

{
	echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "hostname=$(hostname)"
	echo "kernel=$(uname -r)"
	echo "boot_id=$(</proc/sys/kernel/random/boot_id)"
	echo "cmdline=$(</proc/cmdline)"
	echo "test_dir=$TEST_DIR"
	echo "items=$ITEMS"
	echo "workers=$WORKERS"
	echo "cpu_list=$CPU_LIST"
	echo "warmup_rounds=$WARMUP_ROUNDS"
	echo "formal_rounds=$ROUNDS"
	echo "source_sha256=$(sha256sum "$SOURCE" | awk '{print $1}')"
	echo "compiler=$(cc --version | sed -n '1p')"
	echo "loadavg=$(</proc/loadavg)"
	echo "max_user_instances=$(</proc/sys/fs/inotify/max_user_instances)"
	echo "max_user_watches=$(</proc/sys/fs/inotify/max_user_watches)"
	stat -f -c 'test_fs_type=%T test_fs_magic=%t' "$TEST_DIR"
} > "$run_dir/env.txt"
lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ > "$run_dir/cpu-topology.txt"

log_value()
{
	local file=$1 key=$2
	awk -v key="$key" '/^result / {
		for (i = 1; i <= NF; i++) {
			split($i, kv, "=")
			if (kv[1] == key) value=kv[2]
		}
	} END {print value}' "$file"
}

run_one()
{
	local phase=$1 round=$2 position=$3 topology=$4
	local log add remove metric semantic overflow unexpected affinity
	log="$run_dir/logs/${phase}-r$(printf '%02d' "$round")-p${position}-${topology}.log"
	env TEST_DIR="$TEST_DIR" SCENARIO=topology TOPOLOGY="$topology" \
		WORKERS="$WORKERS" ITEMS="$ITEMS" CPU_LIST="$CPU_LIST" KEEP_FILES=0 \
		"$binary" > "$log" 2>&1
	add=$(log_value "$log" add_worker_ns_per_watch)
	remove=$(log_value "$log" remove_worker_ns_per_watch)
	metric=$(awk -v add="$add" -v remove="$remove" \
		'BEGIN {printf "%.6f", add + remove}')
	semantic=$(log_value "$log" semantic_status)
	overflow=$(log_value "$log" overflow_events)
	unexpected=$(log_value "$log" unexpected_results)
	affinity=$(log_value "$log" affinity_ok)
	[[ "$semantic" == PASS && "$overflow" == 0 && "$unexpected" == 0 && \
		"$affinity" == 1 ]] || {
		cat "$log" >&2
		die "semantic gate failed for $phase round $round $topology"
	}
	printf '%s\t%s\t%s\t%s\t%s\tpair_worker_ns_per_watch\t%s\t%s\t%s\t%s\t%s\t%s\n' \
		"$phase" "$round" "$position" "$topology" "$WORKERS" "$metric" "$semantic" \
		"$overflow" "$unexpected" "$affinity" "${log#"$run_dir/"}" >> "$results"
}

for phase in warmup formal; do
	if [[ "$phase" == warmup ]]; then
		phase_rounds=$WARMUP_ROUNDS
	else
		phase_rounds=$ROUNDS
	fi
	for ((round = 1; round <= phase_rounds; round++)); do
		if (( round % 2 == 1 )); then
			run_one "$phase" "$round" 1 distinct
			run_one "$phase" "$round" 2 shared
		else
			run_one "$phase" "$round" 1 shared
			run_one "$phase" "$round" 2 distinct
		fi
	done
done

python3 "$SCRIPT_DIR/summarize_p8.py" "$results" "$run_dir/summary.tsv"
cat "$run_dir/summary.tsv"
echo "run_dir=$run_dir"
