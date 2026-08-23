#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

if [[ $# -ne 3 ]]; then
	echo "usage: $0 <binary> <cold|warm> <output-dir>" >&2
	exit 2
fi

binary=$1
condition=$2
out_dir=$3
tracefs=/sys/kernel/tracing
[[ -x "$binary" ]] || { echo "missing executable: $binary" >&2; exit 1; }
[[ "$condition" == cold || "$condition" == warm ]] || exit 2
sudo -n true
mkdir -p "$out_dir"

reset_trace()
{
	printf '0\n' | sudo -n tee "$tracefs/tracing_on" >/dev/null || true
	printf 'nop\n' | sudo -n tee "$tracefs/current_tracer" >/dev/null || true
	printf '\n' | sudo -n tee "$tracefs/set_ftrace_pid" >/dev/null || true
	printf '\n' | sudo -n tee "$tracefs/set_ftrace_filter" >/dev/null || true
}

pid=
cleanup()
{
	local rc=$?
	reset_trace
	if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
		kill -CONT "$pid" 2>/dev/null || true
		kill "$pid" 2>/dev/null || true
	fi
	return "$rc"
}
trap cleanup EXIT

reset_trace
sudo -n sh -c ": > '$tracefs/trace'"
"$binary" "--trace-$condition" --slots 128 \
	>"$out_dir/$condition.program.out" \
	2>"$out_dir/$condition.program.err" &
pid=$!

for _ in $(seq 1 200); do
	state=$(awk '/^State:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)
	if [[ "$state" == T ]] && grep -q '^trace_ready=1 ' \
		"$out_dir/$condition.program.err"; then
		break
	fi
	sleep 0.05
done
[[ ${state:-} == T ]] || { echo "trace target did not stop" >&2; exit 1; }

printf '%s\n' io_rsrc_node_alloc io_cache_alloc_new |
	sudo -n tee "$tracefs/set_ftrace_filter" >/dev/null
printf '%s\n' "$pid" | sudo -n tee "$tracefs/set_ftrace_pid" >/dev/null
printf 'function\n' | sudo -n tee "$tracefs/current_tracer" >/dev/null
sudo -n sh -c ": > '$tracefs/trace'"
printf '1\n' | sudo -n tee "$tracefs/tracing_on" >/dev/null
kill -CONT "$pid"
wait "$pid"
pid=
printf '0\n' | sudo -n tee "$tracefs/tracing_on" >/dev/null
sudo -n cat "$tracefs/trace" | tee "$out_dir/$condition.trace.txt" >/dev/null

node_alloc=$(grep -c ': io_rsrc_node_alloc <-' \
	"$out_dir/$condition.trace.txt" || true)
fresh_alloc=$(grep -c 'io_cache_alloc_new <-io_rsrc_node_alloc' \
	"$out_dir/$condition.trace.txt" || true)
all_fresh=$(grep -c 'io_cache_alloc_new' "$out_dir/$condition.trace.txt" || true)
other_fresh=$((all_fresh - fresh_alloc))
cache_hit=$((node_alloc - fresh_alloc))
printf 'condition\tnode_alloc\tnode_fresh_alloc\tinferred_node_cache_hit\tother_cache_fresh_alloc\tsemantic_pass\n' \
	>"$out_dir/$condition.trace-summary.tsv"
printf '%s\t%s\t%s\t%s\t%s\t1\n' "$condition" "$node_alloc" \
	"$fresh_alloc" "$cache_hit" "$other_fresh" \
	>>"$out_dir/$condition.trace-summary.tsv"
cat "$out_dir/$condition.trace-summary.tsv"

reset_trace
trap - EXIT
