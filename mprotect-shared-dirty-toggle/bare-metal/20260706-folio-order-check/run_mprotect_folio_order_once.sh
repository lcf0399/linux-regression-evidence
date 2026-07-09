#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOT=${ROOT:-"$HOME/kernel-study"}
SRC="$ROOT/mm_regression_gen/mprotect/4-attribution/baremetal/mprotect_folio_order_probe.c"
BIN=${BIN:-"$ROOT/mm_regression_gen/mprotect/4-attribution/baremetal/mprotect_folio_order_probe"}
OUT_BASE=${OUT_BASE:-"$ROOT/out/baremetal-ab/mprotect_folio_order"}
PIN_CPU=${PIN_CPU:-2}
MAPPING_MB=${MAPPING_MB:-64}

mkdir -p "$OUT_BASE"
gcc -O2 -Wall -Wextra -o "$BIN" "$SRC"

kernel=$(uname -r)
stamp=$(date -u +%Y%m%dT%H%M%SZ)
run_dir="$OUT_BASE/${stamp}_${kernel}"
mkdir -p "$run_dir"

{
	echo "kernel=$kernel"
	echo "pin_cpu=$PIN_CPU"
	echo "mapping_mb=$MAPPING_MB"
	lscpu | grep -E 'Model name|CPU\\(s\\)|NUMA' || true
} > "$run_dir/env.txt"

cmd=(env MAPPING_MB="$MAPPING_MB" "$BIN")
if [[ $EUID -eq 0 ]]; then
	taskset -c "$PIN_CPU" "${cmd[@]}" | tee "$run_dir/raw.log"
else
	sudo -n taskset -c "$PIN_CPU" "${cmd[@]}" | tee "$run_dir/raw.log"
fi

cp "$run_dir/raw.log" "$run_dir/summary.txt"
cat "$run_dir/summary.txt"
echo "run_dir=$run_dir"
