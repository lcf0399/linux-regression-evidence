#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
SOURCE_ROOT=${SOURCE_ROOT:-}
OUT_BASE=${OUT_BASE:-"$SCRIPT_DIR/results"}
EXPECTED_KERNEL=${EXPECTED_KERNEL:-$(uname -r)}
READY_TIMEOUT_SEC=${READY_TIMEOUT_SEC:-90}
EVENT_TIMEOUT_SEC=${EVENT_TIMEOUT_SEC:-10}
TARGETS=(arch drivers fs include kernel net security sound)

die()
{
	echo "fsnotify real-watcher topology: $*" >&2
	exit 2
}

require_positive_integer()
{
	local name=$1 value=$2
	if ! [[ "$value" =~ ^[0-9]+$ ]] || (( value == 0 )); then
		die "$name must be a positive integer, got: $value"
	fi
}

inner_cleanup()
{
	local pid path
	set +e
	for pid in "${WATCHER_PIDS[@]:-}"; do
		if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
			kill -TERM "$pid" 2>/dev/null
		fi
	done
	for pid in "${WATCHER_PIDS[@]:-}"; do
		[[ -n "$pid" ]] && wait "$pid" 2>/dev/null
	done
	for path in "${PROBE_PATHS[@]:-}"; do
		[[ -n "$path" ]] && rm -f -- "$path"
	done
	set -e
}

run_inner()
{
	local run_dir=$1 source_root=$2
	local i slot target pid deadline fdinfo fd watch_count sdev_count sdev
	local source_dir_count probe event_seen ready all_ready=1 all_events=1

	WATCHER_PIDS=()
	PROBE_PATHS=()
	WATCHER_FDS=()
	WATCH_COUNTS=()
	SOURCE_DIR_COUNTS=()
	WATCHER_SDEVS=()
	WATCHER_SDEV_COUNTS=()
	trap inner_cleanup EXIT INT TERM
	mkdir -p "$run_dir/events" "$run_dir/fdinfo" "$run_dir/setup"
	printf 'slot\ttarget\tpid\tfd\twatch_count\tsource_dir_count\tsdev\tsdev_count\tready\tevent_seen\n' \
		> "$run_dir/watchers.tsv"

	for i in "${!TARGETS[@]}"; do
		slot=$(printf 'w%02d' "$((i + 1))")
		target="$source_root/${TARGETS[$i]}"
		[[ -d "$target" ]] || die "missing source subtree: $target"
		inotifywait -m -r -e create -e close_write -e delete \
			--format '%w%f|%e' "$target" \
			> "$run_dir/events/$slot.log" \
			2> "$run_dir/setup/$slot.log" &
		WATCHER_PIDS[i]=$!
		PROBE_PATHS[i]="$target/.codex-fsnotify-reality-gate-$slot-$$"
	done

	deadline=$((SECONDS + READY_TIMEOUT_SEC))
	while (( SECONDS < deadline )); do
		all_ready=1
		for i in "${!TARGETS[@]}"; do
			slot=$(printf 'w%02d' "$((i + 1))")
			pid=${WATCHER_PIDS[$i]}
			kill -0 "$pid" 2>/dev/null || die "watcher $slot exited before readiness"
			if ! grep -Fq 'Watches established.' "$run_dir/setup/$slot.log"; then
				all_ready=0
			fi
		done
		(( all_ready == 1 )) && break
		sleep 0.05
	done
	(( all_ready == 1 )) || die "watchers did not become ready within ${READY_TIMEOUT_SEC}s"

	for i in "${!TARGETS[@]}"; do
		slot=$(printf 'w%02d' "$((i + 1))")
		pid=${WATCHER_PIDS[$i]}
		fdinfo=''
		for fdinfo in "/proc/$pid/fdinfo/"*; do
			if grep -q '^inotify wd:' "$fdinfo" 2>/dev/null; then
				break
		fi
		done
		[[ -n "$fdinfo" && -f "$fdinfo" ]] || die "no inotify fdinfo for watcher $slot"
		fd=${fdinfo##*/}
		cp "$fdinfo" "$run_dir/fdinfo/$slot.txt"
		watch_count=$(grep -c '^inotify wd:' "$fdinfo")
		sdev_count=$(awk '/^inotify wd:/ {
			for (i = 1; i <= NF; i++) if ($i ~ /^sdev:/) {
				sub(/^sdev:/, "", $i); seen[$i] = 1
			}
		} END {for (v in seen) n++; print n+0}' "$fdinfo")
		sdev=$(awk '/^inotify wd:/ {
			for (i = 1; i <= NF; i++) if ($i ~ /^sdev:/) {
				sub(/^sdev:/, "", $i); print $i; exit
			}
		}' "$fdinfo")
		source_dir_count=$(find "$source_root/${TARGETS[$i]}" -type d -printf '.' | wc -c)
		[[ "$watch_count" == "$source_dir_count" ]] || \
			die "$slot watch count $watch_count != source directory count $source_dir_count"
		[[ "$sdev_count" == 1 && -n "$sdev" ]] || die "$slot spans unexpected sdev count $sdev_count"
		WATCHER_FDS[i]=$fd
		WATCH_COUNTS[i]=$watch_count
		SOURCE_DIR_COUNTS[i]=$source_dir_count
		WATCHER_SDEVS[i]=$sdev
		WATCHER_SDEV_COUNTS[i]=$sdev_count
	done

	for i in "${!TARGETS[@]}"; do
		probe=${PROBE_PATHS[$i]}
		printf 'fsnotify-reality-gate\n' > "$probe"
		rm -f -- "$probe"
	done

	deadline=$((SECONDS + EVENT_TIMEOUT_SEC))
	while (( SECONDS < deadline )); do
		all_events=1
		for i in "${!TARGETS[@]}"; do
			slot=$(printf 'w%02d' "$((i + 1))")
			probe=${PROBE_PATHS[$i]}
			if ! grep -Fq "${probe}|" "$run_dir/events/$slot.log"; then
				all_events=0
			fi
		done
		(( all_events == 1 )) && break
		sleep 0.05
	done
	(( all_events == 1 )) || die "not all watchers observed their probe event"

	for i in "${!TARGETS[@]}"; do
		slot=$(printf 'w%02d' "$((i + 1))")
		pid=${WATCHER_PIDS[$i]}
		fd=${WATCHER_FDS[$i]}
		watch_count=${WATCH_COUNTS[$i]}
		source_dir_count=${SOURCE_DIR_COUNTS[$i]}
		sdev_count=${WATCHER_SDEV_COUNTS[$i]}
		sdev=${WATCHER_SDEVS[$i]}
		ready=0
		event_seen=0
		grep -Fq 'Watches established.' "$run_dir/setup/$slot.log" && ready=1
		grep -Fq "${PROBE_PATHS[$i]}|" "$run_dir/events/$slot.log" && event_seen=1
		printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
			"$slot" "${TARGETS[$i]}" "$pid" "$fd" "$watch_count" \
			"$source_dir_count" "$sdev" "$sdev_count" "$ready" "$event_seen" \
			>> "$run_dir/watchers.tsv"
	done

	echo 'inner_semantic_status=PASS'
}

if [[ ${1:-} == --inner ]]; then
	[[ $# -eq 3 ]] || die 'internal invocation requires --inner <run-dir> <source-root>'
	run_inner "$2" "$3"
	exit 0
fi

for cmd in awk date find grep inotifywait lscpu sed sha256sum stat trace-cmd uname; do
	command -v "$cmd" >/dev/null || die "missing required command: $cmd"
done
require_positive_integer READY_TIMEOUT_SEC "$READY_TIMEOUT_SEC"
require_positive_integer EVENT_TIMEOUT_SEC "$EVENT_TIMEOUT_SEC"
[[ -n "$SOURCE_ROOT" ]] || die 'SOURCE_ROOT must name a writable Linux source tree'
[[ -d "$SOURCE_ROOT" ]] || die "missing source root: $SOURCE_ROOT"
[[ -w "$SOURCE_ROOT" ]] || die "source root is not writable: $SOURCE_ROOT"
kernel=$(uname -r)
[[ "$kernel" == "$EXPECTED_KERNEL" ]] || die "running $kernel, expected $EXPECTED_KERNEL"

if (( EUID == 0 )); then
	SUDO=()
	RUN_USER=${SUDO_USER:-root}
	RUN_GROUP=$(id -gn "$RUN_USER")
else
	command -v sudo >/dev/null || die 'sudo is required for trace-cmd'
	sudo -n true
	SUDO=(sudo -n)
	RUN_USER=$(id -un)
	RUN_GROUP=$(id -gn)
fi

TRACEFS=${TRACEFS:-/sys/kernel/tracing}
if ! "${SUDO[@]}" test -r "$TRACEFS/available_filter_functions"; then
	TRACEFS=/sys/kernel/debug/tracing
fi
"${SUDO[@]}" test -r "$TRACEFS/available_filter_functions" || die 'tracefs is unavailable'
"${SUDO[@]}" grep -Eq '^fsnotify_add_mark_locked([[:space:]]|$)' \
	"$TRACEFS/available_filter_functions" || die 'fsnotify_add_mark_locked is unavailable'
"${SUDO[@]}" grep -Eq '^fsnotify_detach_connector_from_object([[:space:]]|$)' \
	"$TRACEFS/available_filter_functions" || die 'fsnotify_detach_connector_from_object is unavailable'
"${SUDO[@]}" test -r "$TRACEFS/events/kmem/kmem_cache_alloc/format" || \
	die 'kmem:kmem_cache_alloc is unavailable'
"${SUDO[@]}" grep -Fq 'char[] name' "$TRACEFS/events/kmem/kmem_cache_alloc/format" || \
	die 'kmem_cache_alloc does not expose the slab name'

mkdir -p "$OUT_BASE"
stamp=$(date -u +%Y%m%dT%H%M%SZ)
run_dir="$OUT_BASE/${stamp}_${kernel}"
mkdir -p "$run_dir"
trace_dat="$run_dir/trace.dat"
trace_report="$run_dir/trace.report.txt"
workload_log="$run_dir/workload.log"

{
	echo "captured_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "hostname=$(hostname)"
	echo "kernel=$kernel"
	echo "cmdline=$(</proc/cmdline)"
	echo "source_root=$SOURCE_ROOT"
	echo "source_git_head=$(git -C "$SOURCE_ROOT" rev-parse HEAD 2>/dev/null || echo unavailable)"
	echo "source_git_describe=$(git -C "$SOURCE_ROOT" describe --always --dirty 2>/dev/null || echo unavailable)"
	echo "source_fs=$(stat -f -c '%T' "$SOURCE_ROOT")"
	echo "source_device=$(stat -c '%d' "$SOURCE_ROOT")"
	echo "inotifywait_path=$(command -v inotifywait)"
	echo "inotifywait_package=$(dpkg-query -W -f='${Version}' inotify-tools 2>/dev/null || echo unavailable)"
	echo "trace_cmd=$(trace-cmd --version 2>&1 | sed -n '1p' || true)"
	echo "runner_sha256=$(sha256sum "$0" | awk '{print $1}')"
	echo "max_user_instances=$(</proc/sys/fs/inotify/max_user_instances)"
	echo "max_user_watches=$(</proc/sys/fs/inotify/max_user_watches)"
	echo "max_queued_events=$(</proc/sys/fs/inotify/max_queued_events)"
} > "$run_dir/env.txt"
lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE > "$run_dir/cpu-topology.txt"
[[ -r "/boot/config-$kernel" ]] && cp "/boot/config-$kernel" "$run_dir/kernel.config"

set +e
"${SUDO[@]}" trace-cmd record -q -p function -F -c \
	-l fsnotify_add_mark_locked \
	-l fsnotify_detach_connector_from_object \
	-e kmem:kmem_cache_alloc \
	-f 'name == "fsnotify_inode_mark_connector"' \
	-o "$trace_dat" --user "$RUN_USER" -- \
	"$0" --inner "$run_dir" "$SOURCE_ROOT" > "$workload_log" 2>&1
trace_rc=$?
set -e
"${SUDO[@]}" trace-cmd report -i "$trace_dat" > "$trace_report"
"${SUDO[@]}" chown -R "$RUN_USER:$RUN_GROUP" "$run_dir"

[[ -s "$run_dir/watchers.tsv" ]] || die "inner run failed before watchers.tsv; see $workload_log"

awk '
	BEGIN {OFS="\t"}
	/function:[[:space:]]+fsnotify_add_mark_locked/ {
		pid=$1; sub(/^.*-/, "", pid); add[pid]++
	}
	/kmem_cache_alloc:/ && /name=fsnotify_inode_mark_connector/ {
		pid=$1; sub(/^.*-/, "", pid); alloc[pid]++
	}
	/function:[[:space:]]+fsnotify_detach_connector_from_object/ {
		pid=$1; sub(/^.*-/, "", pid); detach[pid]++
	}
	END {
		print "pid", "add_mark", "connector_alloc", "detach_connector"
		for (pid in add) print pid, add[pid]+0, alloc[pid]+0, detach[pid]+0
	}' "$trace_report" | sort -t $'\t' -k1,1n > "$run_dir/trace-per-pid.tsv"

awk '
	BEGIN {OFS="\t"}
	/function:[[:space:]]+fsnotify_add_mark_locked/ {
		pid=$1; sub(/^.*-/, "", pid)
		cpu=$2; gsub(/^\[/, "", cpu); gsub(/\]$/, "", cpu)
		ts=$4; sub(/:$/, "", ts); bucket=int(ts * 1000)
		adds++; pids[pid]=1; cpus[cpu]=1; buckets[bucket SUBSEP pid]=1
		if (previous != "" && previous != pid) transitions++
		previous=pid
	}
	END {
		for (v in pids) pid_count++
		for (v in cpus) cpu_count++
		for (v in buckets) {split(v, a, SUBSEP); bucket_pids[a[1]]++}
		for (b in bucket_pids) {
			if (bucket_pids[b] > 1) multi_pid_ms++
			if (bucket_pids[b] > max_pids_ms) max_pids_ms=bucket_pids[b]
		}
		print "metric", "value"
		print "add_mark_total", adds+0
		print "add_mark_unique_pids", pid_count+0
		print "add_mark_unique_cpus", cpu_count+0
		print "add_mark_pid_transitions", transitions+0
		print "multi_pid_1ms_buckets", multi_pid_ms+0
		print "max_pids_in_1ms_bucket", max_pids_ms+0
	}' "$trace_report" > "$run_dir/trace-topology.tsv"

watcher_rows=$(awk 'NR > 1 {n++} END {print n+0}' "$run_dir/watchers.tsv")
watch_total=$(awk 'NR > 1 {n += $5} END {print n+0}' "$run_dir/watchers.tsv")
source_dir_total=$(awk 'NR > 1 {n += $6} END {print n+0}' "$run_dir/watchers.tsv")
ready_count=$(awk 'NR > 1 && $9 == 1 {n++} END {print n+0}' "$run_dir/watchers.tsv")
event_count=$(awk 'NR > 1 && $10 == 1 {n++} END {print n+0}' "$run_dir/watchers.tsv")
unique_sdev=$(awk 'NR > 1 {seen[$7]=1} END {for (v in seen) n++; print n+0}' "$run_dir/watchers.tsv")
single_sdev_rows=$(awk 'NR > 1 && $8 == 1 {n++} END {print n+0}' "$run_dir/watchers.tsv")
add_total=$(awk '$1 == "add_mark_total" {print $2}' "$run_dir/trace-topology.tsv")
add_pids=$(awk '$1 == "add_mark_unique_pids" {print $2}' "$run_dir/trace-topology.tsv")
add_cpus=$(awk '$1 == "add_mark_unique_cpus" {print $2}' "$run_dir/trace-topology.tsv")
pid_transitions=$(awk '$1 == "add_mark_pid_transitions" {print $2}' "$run_dir/trace-topology.tsv")
multi_pid_ms=$(awk '$1 == "multi_pid_1ms_buckets" {print $2}' "$run_dir/trace-topology.tsv")
max_pids_ms=$(awk '$1 == "max_pids_in_1ms_bucket" {print $2}' "$run_dir/trace-topology.tsv")
alloc_total=$(awk 'NR > 1 {n += $3} END {print n+0}' "$run_dir/trace-per-pid.tsv")
detach_total=$(awk 'NR > 1 {n += $4} END {print n+0}' "$run_dir/trace-per-pid.tsv")
alloc_pids=$(awk 'NR > 1 && $3 > 0 {seen[$1]=1} END {for (v in seen) n++; print n+0}' "$run_dir/trace-per-pid.tsv")
detach_pids=$(awk 'NR > 1 && $4 > 0 {seen[$1]=1} END {for (v in seen) n++; print n+0}' "$run_dir/trace-per-pid.tsv")

gate=PASS
if (( trace_rc != 0 || watcher_rows != 8 || ready_count != 8 || event_count != 8 ||
	  watch_total != source_dir_total || unique_sdev != 1 || single_sdev_rows != 8 ||
	  add_total != watch_total || alloc_total != watch_total || detach_total != watch_total ||
	  add_pids != 8 || alloc_pids != 8 || detach_pids != 8 || add_cpus < 2 ||
	  pid_transitions <= 7 || multi_pid_ms < 1 || max_pids_ms < 2 )); then
	gate=FAIL
fi

{
	echo "gate_status=$gate"
	echo "trace_command_exit=$trace_rc"
	echo "watcher_rows=$watcher_rows"
	echo "ready_watchers=$ready_count"
	echo "semantic_event_watchers=$event_count"
	echo "watch_total=$watch_total"
	echo "source_dir_total=$source_dir_total"
	echo "unique_sdev=$unique_sdev"
	echo "single_sdev_rows=$single_sdev_rows"
	echo "add_mark_total=$add_total"
	echo "connector_alloc_total=$alloc_total"
	echo "detach_connector_total=$detach_total"
	echo "add_mark_unique_pids=$add_pids"
	echo "connector_alloc_unique_pids=$alloc_pids"
	echo "detach_connector_unique_pids=$detach_pids"
	echo "add_mark_unique_cpus=$add_cpus"
	echo "add_mark_pid_transitions=$pid_transitions"
	echo "multi_pid_1ms_buckets=$multi_pid_ms"
	echo "max_pids_in_1ms_bucket=$max_pids_ms"
} | tee "$run_dir/gate.env"

[[ "$gate" == PASS ]]
