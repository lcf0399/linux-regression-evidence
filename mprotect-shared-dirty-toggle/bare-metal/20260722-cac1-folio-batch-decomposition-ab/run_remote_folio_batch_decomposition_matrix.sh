#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

if [[ $# -ne 1 ]]; then
	echo "usage: $0 <ssh-host>" >&2
	exit 2
fi

HOST=$1
REMOTE_ROOT=${REMOTE_ROOT:-/home/lcf/kernel-study}
BASE=${BASE:-$REMOTE_ROOT/linux-baremetal/mprotect-cac1-folio-decomp-20260722}
RESCUE_RELEASE=${RESCUE_RELEASE:-7.0.0-27-generic}
CONNECT_TIMEOUT=${CONNECT_TIMEOUT:-5}
DOWN_TIMEOUT=${DOWN_TIMEOUT:-90}
BOOT_TIMEOUT=${BOOT_TIMEOUT:-600}
BOOT_SETTLE_SECONDS=${BOOT_SETTLE_SECONDS:-60}
START_AT=${START_AT:-parent-a}
HELPERS=./scripts/baremetal/kernel
POINT_RUNNER=${POINT_RUNNER:-./linux-regression-evidence/mprotect-shared-dirty-toggle/bare-metal/20260722-cac1-folio-batch-decomposition-ab/run_folio_batch_decomposition_point.sh}

POINT_ROLES=(parent child fastpath folioonly nofolio folioonly fastpath child parent)
POINT_LABELS=(parent-a child-a fastpath-a folioonly-a nofolio folioonly-b fastpath-b child-b parent-b)

fail()
{
	echo "folio/batch remote matrix failed: $*" >&2
	exit 1
}

ssh_remote()
{
	ssh -o BatchMode=yes -o ConnectTimeout="$CONNECT_TIMEOUT" \
		-o ServerAliveInterval=5 -o ServerAliveCountMax=3 "$HOST" "$@"
}

manifest_value()
{
	local role=$1
	local key=$2
	ssh_remote "awk -F '\t' -v key='$key' '\$1 == key { print \$2; exit }' '$BASE/build-logs/$role.artifacts.tsv'"
}

request_reboot()
{
	local rc
	set +e
	ssh_remote sudo systemctl reboot
	rc=$?
	set -e
	(( rc == 0 || rc == 255 )) || fail "reboot request returned SSH status $rc"
}

wait_for_new_boot()
{
	local old_boot_id=$1
	local expected_release=$2
	local deadline state rc
	local down_seen=0
	NEW_BOOT_ID=
	RUNNING_RELEASE=

	deadline=$((SECONDS + DOWN_TIMEOUT))
	while (( SECONDS < deadline )); do
		if ! ssh_remote true >/dev/null 2>&1; then
			down_seen=1
			break
		fi
		sleep 2
	done
	echo "ssh_down_seen=$down_seen"

	deadline=$((SECONDS + BOOT_TIMEOUT))
	while (( SECONDS < deadline )); do
		set +e
		# The substitutions are intentionally evaluated by the remote shell.
		# shellcheck disable=SC2016
		state=$(ssh_remote 'printf "%s\t%s\n" "$(cat /proc/sys/kernel/random/boot_id)" "$(uname -r)"' 2>/dev/null)
		rc=$?
		set -e
		if (( rc == 0 )); then
			IFS=$'\t' read -r NEW_BOOT_ID RUNNING_RELEASE <<< "$state"
			if [[ -n "$NEW_BOOT_ID" && "$NEW_BOOT_ID" != "$old_boot_id" ]]; then
				break
			fi
		fi
		sleep 5
	done

	[[ -n "${NEW_BOOT_ID:-}" && "$NEW_BOOT_ID" != "$old_boot_id" ]] ||
		fail "no new boot ID appeared within ${BOOT_TIMEOUT}s"
	[[ "$RUNNING_RELEASE" == "$expected_release" ]] ||
		fail "running $RUNNING_RELEASE after reboot, expected $expected_release"
	echo "new_boot_id=$NEW_BOOT_ID"
	echo "running_release=$RUNNING_RELEASE"
}

ensure_rescue()
{
	local release
	release=$(ssh_remote uname -r)
	[[ "$release" == "$RESCUE_RELEASE" ]] ||
		fail "machine is running $release, expected rescue $RESCUE_RELEASE"
	ssh_remote "cd '$REMOTE_ROOT' && RESCUE_RELEASE='$RESCUE_RELEASE' '$HELPERS/prepare_stable_kernel_boot_smoke.sh'"
}

run_point()
{
	local role=$1
	local label=$2
	local manifest="$BASE/build-logs/$role.artifacts.tsv"
	local release old_boot_id rescue_boot_id

	release=$(manifest_value "$role" kernelrelease)
	[[ -n "$release" ]] || fail "missing kernelrelease for role $role"

	echo "point_start role=$role label=$label release=$release"
	ensure_rescue
	old_boot_id=$(ssh_remote cat /proc/sys/kernel/random/boot_id)
	ssh_remote "cd '$REMOTE_ROOT' && WORK='$BASE' ARTIFACT_MANIFEST_OVERRIDE='$manifest' KERNEL_RELEASE_OVERRIDE='$release' REQUIRED_KERNEL_CMDLINE=preempt=none '$HELPERS/select_stable_kernel_efi_bootnext.sh' '$role'"
	request_reboot
	wait_for_new_boot "$old_boot_id" "$release"
	ssh_remote "tr ' ' '\n' < /proc/cmdline | grep -Fxq preempt=none" ||
		fail "$release did not boot with preempt=none"

	ssh_remote "cd '$REMOTE_ROOT' && BOOT_SETTLE_SECONDS='$BOOT_SETTLE_SECONDS' '$POINT_RUNNER' '$role' '$label'"

	rescue_boot_id=$(ssh_remote cat /proc/sys/kernel/random/boot_id)
	request_reboot
	wait_for_new_boot "$rescue_boot_id" "$RESCUE_RELEASE"
	ensure_rescue
	echo "point_complete role=$role label=$label release=$release"
}

START_INDEX=-1
for index in "${!POINT_LABELS[@]}"; do
	if [[ ${POINT_LABELS[$index]} == "$START_AT" ]]; then
		START_INDEX=$index
		break
	fi
done
(( START_INDEX >= 0 )) ||
	fail "START_AT must be one of: ${POINT_LABELS[*]}"

ensure_rescue
for ((index = START_INDEX; index < ${#POINT_LABELS[@]}; index++)); do
	run_point "${POINT_ROLES[$index]}" "${POINT_LABELS[$index]}"
done

echo "matrix_complete=yes"
echo "matrix_design=${POINT_LABELS[*]}"
echo "executed_from=$START_AT"
