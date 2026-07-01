#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SRC=${SRC:-"$SCRIPT_DIR/single_protect_reproducer.c"}
BIN=${BIN:-"$SCRIPT_DIR/single_protect_reproducer"}
OUT_BASE=${OUT_BASE:-"$SCRIPT_DIR/out"}
PIN_CPU=${PIN_CPU:-2}
EXTERNAL_ROUNDS=${EXTERNAL_ROUNDS:-5}
MAPPING_MB=${MAPPING_MB:-64}
ITERATIONS=${ITERATIONS:-200}
WARMUP=${WARMUP:-5}

mkdir -p "$OUT_BASE"
cc -O2 -Wall -Wextra -o "$BIN" "$SRC"

kernel=$(uname -r)
stamp=$(date -u +%Y%m%dT%H%M%SZ)
run_dir="$OUT_BASE/${stamp}_${kernel}"
mkdir -p "$run_dir"

{
	echo "kernel=$kernel"
	echo "pin_cpu=$PIN_CPU"
	echo "external_rounds=$EXTERNAL_ROUNDS"
	echo "mapping_mb=$MAPPING_MB"
	echo "iterations=$ITERATIONS"
	echo "warmup=$WARMUP"
	lscpu | grep -E 'Model name|CPU\\(s\\)|NUMA' || true
} > "$run_dir/env.txt"

: > "$run_dir/raw.log"
for round in $(seq 1 "$EXTERNAL_ROUNDS"); do
	echo "round=$round" | tee -a "$run_dir/raw.log" >/dev/null
	taskset -c "$PIN_CPU" env \
		MAPPING_MB="$MAPPING_MB" \
		ITERATIONS="$ITERATIONS" \
		WARMUP="$WARMUP" \
		EXTERNAL_ROUNDS=1 \
		"$BIN" | tee -a "$run_dir/raw.log"
done

awk '
BEGIN {
  print "round\texpected_match_ratio\tunexpected_results\tsetup_ns_per_page\tsingle_protect_ns_per_page\ttotal_ns_per_page"
}
/scenario=shared_dirty_single_protect/ {
  round += 1
  delete v
  for (i = 1; i <= NF; i++) {
    split($i, kv, "=")
    v[kv[1]] = kv[2]
  }
  print round "\t" v["expected_match_ratio"] "\t" v["unexpected_results"] "\t" v["setup_ns_per_page"] "\t" v["single_protect_ns_per_page"] "\t" v["total_ns_per_page"]
}
' "$run_dir/raw.log" > "$run_dir/summary.tsv"

cat "$run_dir/summary.tsv"
echo "run_dir=$run_dir"
