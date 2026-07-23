#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOT=${ROOT:-/home/lcf/kernel-study}
EXACT_BASE=${EXACT_BASE:-$ROOT/linux-baremetal/mprotect-pedro-v3-exact-20260722}
BASE=${BASE:-$ROOT/linux-baremetal/mprotect-v713-shared-hint-20260722}
REPRODUCER_SOURCE=${REPRODUCER_SOURCE:-$BASE/workload/mprotect_shared_dirty_reproducer.c}
BASELINE_ARTIFACT="$EXACT_BASE/build-logs/child.artifacts.tsv"
CANDIDATE_ARTIFACT="$BASE/build-logs/candidate.artifacts.tsv"
CANDIDATE_SOURCE_MANIFEST="$BASE/manifests/candidate.source.tsv"

fail()
{
	echo "candidate finalization failed: $*" >&2
	exit 1
}

manifest_value()
{
	local manifest=$1
	local key=$2
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$manifest"
}

sudo -n true || fail "passwordless sudo is required"
[[ $(uname -r) == 7.0.0-27-generic ]] ||
	fail "finalization must run from the 7.0.0-27-generic rescue kernel"
for path in "$BASELINE_ARTIFACT" "$CANDIDATE_ARTIFACT" \
	"$CANDIDATE_SOURCE_MANIFEST" "$REPRODUCER_SOURCE"; do
	[[ -s "$path" ]] || fail "missing prerequisite $path"
done

BASELINE_RELEASE=$(manifest_value "$BASELINE_ARTIFACT" kernelrelease)
CANDIDATE_RELEASE=$(manifest_value "$CANDIDATE_ARTIFACT" kernelrelease)
CANONICAL_CONFIG=$(manifest_value "$BASELINE_ARTIFACT" canonical_config_sha256)
PATCH_SHA256=$(manifest_value "$CANDIDATE_ARTIFACT" candidate_patch_sha256)
[[ -n "$BASELINE_RELEASE" && -n "$CANDIDATE_RELEASE" ]] ||
	fail "missing kernel releases in artifact manifests"
[[ $(manifest_value "$CANDIDATE_ARTIFACT" canonical_config_sha256) == \
	"$CANONICAL_CONFIG" ]] || fail "candidate and baseline canonical configs differ"

for path in "/boot/vmlinuz-$CANDIDATE_RELEASE" \
	"/boot/System.map-$CANDIDATE_RELEASE" "/boot/config-$CANDIDATE_RELEASE" \
	"/boot/initrd.img-$CANDIDATE_RELEASE" \
	"/lib/modules/$CANDIDATE_RELEASE/modules.dep"; do
	sudo test -s "$path" || fail "missing installed artifact $path"
done
[[ $(sha256sum "/boot/config-$CANDIDATE_RELEASE" | awk '{ print $1 }') == \
	$(manifest_value "$CANDIDATE_ARTIFACT" config_sha256) ]] ||
	fail "installed candidate config hash mismatch"
[[ $(sha256sum "/boot/vmlinuz-$CANDIDATE_RELEASE" | awk '{ print $1 }') == \
	$(manifest_value "$CANDIDATE_ARTIFACT" bzimage_sha256) ]] ||
	fail "installed candidate kernel hash mismatch"

mkdir -p "$BASE"/{build-logs,install-logs,workload}
{
	printf 'role\tcandidate\n'
	printf 'kernelrelease\t%s\n' "$CANDIDATE_RELEASE"
	printf 'config_sha256\t%s\n' \
		"$(sha256sum "/boot/config-$CANDIDATE_RELEASE" | awk '{ print $1 }')"
	printf 'vmlinuz_sha256\t%s\n' \
		"$(sha256sum "/boot/vmlinuz-$CANDIDATE_RELEASE" | awk '{ print $1 }')"
	printf 'initrd_sha256\t%s\n' \
		"$(sudo sha256sum "/boot/initrd.img-$CANDIDATE_RELEASE" | awk '{ print $1 }')"
	printf 'modules_dep_sha256\t%s\n' \
		"$(sha256sum "/lib/modules/$CANDIDATE_RELEASE/modules.dep" | awk '{ print $1 }')"
} > "$BASE/install-logs/candidate.installed.tsv"
awk -F '\t' 'BEGIN { OFS="\t" } $1 == "role" { $2="baseline" } { print }' \
	"$BASELINE_ARTIFACT" > "$BASE/build-logs/baseline.artifacts.tsv"
WORKLOAD_TARGET="$BASE/workload/mprotect_shared_dirty_reproducer.c"
if [[ $(readlink -f "$REPRODUCER_SOURCE") != $(readlink -f "$WORKLOAD_TARGET") ]]; then
	install -m 0644 "$REPRODUCER_SOURCE" "$WORKLOAD_TARGET"
fi

sudo update-grub
sudo grep -Fq 'set default="Advanced options for Ubuntu>Ubuntu, with Linux 7.0.0-27-generic"' \
	/boot/grub/grub.cfg || fail "rescue kernel is no longer the GRUB default"
[[ -z $(sudo efibootmgr | awk -F': ' '$1 == "BootNext" { print $2 }') ]] ||
	fail "another BootNext is pending"

{
	printf 'status\tcomplete\n'
	printf 'source_commit\t%s\n' \
		"$(manifest_value "$CANDIDATE_SOURCE_MANIFEST" base_commit)"
	printf 'baseline_release\t%s\n' "$BASELINE_RELEASE"
	printf 'candidate_release\t%s\n' "$CANDIDATE_RELEASE"
	printf 'canonical_config_sha256\t%s\n' "$CANONICAL_CONFIG"
	printf 'candidate_patch_sha256\t%s\n' "$PATCH_SHA256"
	printf 'required_kernel_cmdline\tpreempt=none\n'
	printf 'completed_utc\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$BASE/build-logs/matrix-status.env"

cat "$BASE/build-logs/machine-code-audit.tsv"
cat "$BASE/build-logs/matrix-status.env"
df -h / /boot/efi
