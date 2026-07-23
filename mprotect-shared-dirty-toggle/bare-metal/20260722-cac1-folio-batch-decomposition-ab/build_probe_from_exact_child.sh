#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOT=${ROOT:-/home/lcf/kernel-study}
EXACT_BASE=${EXACT_BASE:-$ROOT/linux-baremetal/mprotect-cac1-exact-20260721}
BASE=${BASE:?BASE must name the diagnostic build workspace}
PATCH_FILE=${PATCH_FILE:?PATCH_FILE must name the diagnostic patch}
PROBE_LOCALVERSION=${PROBE_LOCALVERSION:?PROBE_LOCALVERSION is required}
DIAGNOSTIC=${DIAGNOSTIC:?DIAGNOSTIC is required}
SOURCE_MARKER=${SOURCE_MARKER:?SOURCE_MARKER is required}
AUDIT_GATE=${AUDIT_GATE:?AUDIT_GATE is required}
JOBS=${JOBS:-$(nproc)}
KBUILD_BUILD_TIMESTAMP=${KBUILD_BUILD_TIMESTAMP:-2026-07-21 00:00:00 UTC}
KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-kernel-study}
KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-linux-perf-bm}
KBUILD_BUILD_VERSION=${KBUILD_BUILD_VERSION:-1}

CHILD_COMMIT=cac1db8c3aad97d6ffb56ced8868d6cbbbd2bfbe
CHILD_ARCHIVE="$EXACT_BASE/tarballs/linux-$CHILD_COMMIT.tar.gz"
CHILD_CONFIG="$EXACT_BASE/sources/linux-child/.config"
CHILD_SOURCE_MANIFEST="$EXACT_BASE/manifests/child.source.tsv"
CHILD_ARTIFACT_MANIFEST="$EXACT_BASE/build-logs/child.artifacts.tsv"
PARENT_ARTIFACT_MANIFEST="$EXACT_BASE/build-logs/parent.artifacts.tsv"
SHARED_SIGNING_KEY="$EXACT_BASE/keys/mprotect-cac1-signing-key.pem"
SOURCE="$BASE/sources/linux-probe"

mkdir -p "$BASE"/{tarballs,sources,manifests,prep-logs,build-logs,install-logs,keys,workload}
exec > >(tee "$BASE/prepare-build-install.$(date -u +%Y%m%dT%H%M%SZ).log") 2>&1

fail()
{
	echo "mprotect diagnostic probe preparation failed: $*" >&2
	exit 1
}

manifest_value()
{
	local manifest=$1
	local key=$2
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$manifest"
}

canonical_config_sha256()
{
	sed '/^CONFIG_LOCALVERSION=/d' "$1" | sha256sum | awk '{ print $1 }'
}

symbol_size_hex()
{
	local image=$1
	local symbol=$2
	nm -S --size-sort "$image" |
		awk -v symbol="$symbol" '$4 == symbol { value = $2 } END { if (value != "") print value }'
}

symbol_call_count()
{
	local image=$1
	local caller=$2
	local callee=$3
	local count
	count=$(objdump -dr --disassemble="$caller" "$image" |
		grep -Ec "call.*<$callee([.+>]|$)" || true)
	printf '%s\n' "$count"
}

sudo -n true || fail "passwordless sudo is required"
[[ $(uname -r) == 7.0.0-27-generic ]] ||
	fail "build/install must start from the 7.0.0-27-generic rescue kernel"
[[ -z $(sudo efibootmgr | awk -F': ' '$1 == "BootNext" { print $2 }') ]] ||
	fail "another BootNext is pending"

for path in "$CHILD_ARCHIVE" "$CHILD_CONFIG" "$CHILD_SOURCE_MANIFEST" \
	"$CHILD_ARTIFACT_MANIFEST" "$PARENT_ARTIFACT_MANIFEST" \
	"$SHARED_SIGNING_KEY" "$PATCH_FILE"; do
	[[ -s "$path" ]] || fail "missing prerequisite $path"
done

[[ $(sha256sum "$CHILD_ARCHIVE" | awk '{ print $1 }') == \
	$(manifest_value "$CHILD_SOURCE_MANIFEST" archive_sha256) ]] ||
	fail "exact child archive hash mismatch"
[[ $(sha256sum "$CHILD_CONFIG" | awk '{ print $1 }') == \
	$(manifest_value "$CHILD_ARTIFACT_MANIFEST" config_sha256) ]] ||
	fail "exact child config hash mismatch"
[[ $(sha256sum "$SHARED_SIGNING_KEY" | awk '{ print $1 }') == \
	$(manifest_value "$CHILD_ARTIFACT_MANIFEST" module_signing_pem_sha256) ]] ||
	fail "shared signing key hash mismatch"
[[ $(gcc --version | head -n1) == \
	$(manifest_value "$CHILD_ARTIFACT_MANIFEST" compiler) ]] ||
	fail "compiler identity differs from the exact parent/child build"

mapfile -t PATCH_TARGETS < <(sed -n 's,^+++ b/,,p' "$PATCH_FILE")
[[ ${#PATCH_TARGETS[@]} -eq 1 && ${PATCH_TARGETS[0]} == mm/mprotect.c ]] ||
	fail "diagnostic patch must target only mm/mprotect.c"

[[ ! -e "$SOURCE" ]] || fail "source directory already exists: $SOURCE"
mkdir "$SOURCE"
TOP=$(tar -tzf "$CHILD_ARCHIVE" | sed -n '1s,/.*,,p')
[[ "$TOP" == "linux-$CHILD_COMMIT" ]] ||
	fail "archive top directory is $TOP, expected linux-$CHILD_COMMIT"
tar -xzf "$CHILD_ARCHIVE" -C "$SOURCE" --strip-components=1

BEFORE_MPROTECT_SHA256=$(sha256sum "$SOURCE/mm/mprotect.c" | awk '{ print $1 }')
[[ "$BEFORE_MPROTECT_SHA256" == \
	$(manifest_value "$CHILD_SOURCE_MANIFEST" mm_mprotect_c_sha256) ]] ||
	fail "extracted child mm/mprotect.c hash mismatch"

(
	cd "$SOURCE"
	patch --dry-run -p1 < "$PATCH_FILE"
	patch -p1 < "$PATCH_FILE"
	patch --reverse --dry-run -p1 < "$PATCH_FILE"
)

grep -Fq "$SOURCE_MARKER" \
	"$SOURCE/mm/mprotect.c" || fail "diagnostic source marker is absent"
AFTER_MPROTECT_SHA256=$(sha256sum "$SOURCE/mm/mprotect.c" | awk '{ print $1 }')
[[ "$AFTER_MPROTECT_SHA256" != "$BEFORE_MPROTECT_SHA256" ]] ||
	fail "diagnostic patch did not change mm/mprotect.c"

cp "$CHILD_CONFIG" "$SOURCE/.config"
(
	cd "$SOURCE"
	scripts/config --set-str LOCALVERSION "$PROBE_LOCALVERSION"
	scripts/config --disable LOCALVERSION_AUTO
	make olddefconfig
)

PROBE_CANONICAL=$(canonical_config_sha256 "$SOURCE/.config")
EXACT_CANONICAL=$(manifest_value "$CHILD_ARTIFACT_MANIFEST" canonical_config_sha256)
[[ "$PROBE_CANONICAL" == "$EXACT_CANONICAL" ]] || {
	diff -u \
		<(sed '/^CONFIG_LOCALVERSION=/d' "$CHILD_CONFIG") \
		<(sed '/^CONFIG_LOCALVERSION=/d' "$SOURCE/.config") || true
	fail "probe canonical config differs from exact child"
}

grep -qx 'CONFIG_PREEMPT=y' "$SOURCE/.config"
grep -qx 'CONFIG_PREEMPT_DYNAMIC=y' "$SOURCE/.config"
grep -Fqx "CONFIG_MODULE_SIG_KEY=\"$SHARED_SIGNING_KEY\"" "$SOURCE/.config"

make -C "$SOURCE" clean
rm -f "$SOURCE/certs/signing_key.pem" "$SOURCE/certs/signing_key.x509"

KERNELVERSION=$(make -s -C "$SOURCE" kernelversion)
KERNELRELEASE=$(make -s -C "$SOURCE" kernelrelease)
CHILD_RELEASE=$(manifest_value "$CHILD_ARTIFACT_MANIFEST" kernelrelease)
[[ ${#KERNELRELEASE} -eq ${#CHILD_RELEASE} ]] ||
	fail "probe and child release strings have different lengths"

cat > "$BASE/manifests/probe.source.tsv" <<EOF
role	probe
base_commit	$CHILD_COMMIT
commit	$CHILD_COMMIT
diagnostic	$DIAGNOSTIC
archive_path	$CHILD_ARCHIVE
archive_sha256	$(sha256sum "$CHILD_ARCHIVE" | awk '{ print $1 }')
patch_path	$PATCH_FILE
patch_sha256	$(sha256sum "$PATCH_FILE" | awk '{ print $1 }')
patch_targets	mm/mprotect.c
before_mm_mprotect_c_sha256	$BEFORE_MPROTECT_SHA256
after_mm_mprotect_c_sha256	$AFTER_MPROTECT_SHA256
config_source	$CHILD_CONFIG
config_sha256	$(sha256sum "$SOURCE/.config" | awk '{ print $1 }')
canonical_config_sha256	$PROBE_CANONICAL
kernelversion	$KERNELVERSION
kernelrelease	$KERNELRELEASE
localversion	$PROBE_LOCALVERSION
preemption_contract	dynamic-preempt-boot-none
required_kernel_cmdline	preempt=none
EOF

COMPILER_LAUNCHER=none
MAKE_ARGS=(-j"$JOBS")
if command -v ccache >/dev/null 2>&1; then
	export CCACHE_DIR=${CCACHE_DIR:-$ROOT/linux-baremetal/ccache}
	export CCACHE_BASEDIR=$BASE/sources
	export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-20G}
	export CCACHE_NOHASHDIR=true
	export CCACHE_COMPRESS=true
	MAKE_ARGS+=(CC="ccache gcc")
	COMPILER_LAUNCHER=$(ccache --version | head -n1)
fi

export KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_USER KBUILD_BUILD_HOST KBUILD_BUILD_VERSION
BUILD_LOG="$BASE/build-logs/probe.build.$(date -u +%Y%m%dT%H%M%SZ).log"
echo "building mprotect diagnostic probe release=$KERNELRELEASE jobs=$JOBS log=$BUILD_LOG"
make -C "$SOURCE" "${MAKE_ARGS[@]}" 2>&1 | tee "$BUILD_LOG"

[[ $(make -s -C "$SOURCE" kernelrelease) == "$KERNELRELEASE" ]] ||
	fail "kernelrelease changed after build"
[[ $(canonical_config_sha256 "$SOURCE/.config") == "$EXACT_CANONICAL" ]] ||
	fail "canonical config changed after build"
for path in arch/x86/boot/bzImage vmlinux System.map certs/signing_key.x509; do
	[[ -s "$SOURCE/$path" ]] || fail "missing built artifact $SOURCE/$path"
done

PARENT_VMLINUX="$EXACT_BASE/sources/linux-parent/vmlinux"
CHILD_VMLINUX="$EXACT_BASE/sources/linux-child/vmlinux"
PROBE_VMLINUX="$SOURCE/vmlinux"
for path in "$PARENT_VMLINUX" "$CHILD_VMLINUX" "$PROBE_VMLINUX"; do
	[[ -s "$path" ]] || fail "missing vmlinux for machine-code audit: $path"
done

for role in parent child probe; do
	case "$role" in
		parent) image=$PARENT_VMLINUX ;;
		child) image=$CHILD_VMLINUX ;;
		probe) image=$PROBE_VMLINUX ;;
	esac
	objdump -dr --disassemble=change_pte_range "$image" \
		> "$BASE/build-logs/$role.change_pte_range.objdump.txt"
done

printf 'role\tchange_pte_range_size_hex\tprot_commit_flush_ptes_size_hex\tchange_pte_range_calls_to_helper\tchange_pte_range_calls_to_can_change_pte_writable\tchange_pte_range_calls_to_vm_normal_folio\n' \
	> "$BASE/build-logs/machine-code-audit.tsv"
for role in parent child probe; do
	case "$role" in
		parent) image=$PARENT_VMLINUX ;;
		child) image=$CHILD_VMLINUX ;;
		probe) image=$PROBE_VMLINUX ;;
	esac
	change_size=$(symbol_size_hex "$image" change_pte_range)
	helper_size=$(symbol_size_hex "$image" prot_commit_flush_ptes)
	helper_calls=$(symbol_call_count "$image" change_pte_range prot_commit_flush_ptes)
	can_change_calls=$(symbol_call_count "$image" change_pte_range can_change_pte_writable)
	vm_normal_calls=$(symbol_call_count "$image" change_pte_range vm_normal_folio)
	printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$role" "${change_size:-absent}" \
		"${helper_size:-absent}" "$helper_calls" "$can_change_calls" \
		"$vm_normal_calls" \
		>> "$BASE/build-logs/machine-code-audit.tsv"
done

CHILD_HELPER_SIZE=$(awk -F '\t' '$1 == "child" { print $3 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
CHILD_HELPER_CALLS=$(awk -F '\t' '$1 == "child" { print $4 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
PROBE_HELPER_SIZE=$(awk -F '\t' '$1 == "probe" { print $3 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
PROBE_HELPER_CALLS=$(awk -F '\t' '$1 == "probe" { print $4 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
CHILD_CAN_CHANGE_CALLS=$(awk -F '\t' '$1 == "child" { print $5 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
PROBE_CAN_CHANGE_CALLS=$(awk -F '\t' '$1 == "probe" { print $5 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
CHILD_VM_NORMAL_CALLS=$(awk -F '\t' '$1 == "child" { print $6 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
PROBE_VM_NORMAL_CALLS=$(awk -F '\t' '$1 == "probe" { print $6 }' \
	"$BASE/build-logs/machine-code-audit.tsv")
case "$AUDIT_GATE" in
	inline-helper)
		[[ "$CHILD_HELPER_SIZE" != absent && "$CHILD_HELPER_CALLS" -ge 1 ]] ||
			fail "exact child does not contain the expected out-of-line helper call"
		[[ "$PROBE_HELPER_SIZE" == absent && "$PROBE_HELPER_CALLS" -eq 0 ]] ||
			fail "probe still contains an out-of-line helper or call"
		;;
	single-pte-fastpath)
		[[ "$CHILD_CAN_CHANGE_CALLS" -eq 0 ]] ||
			fail "exact child unexpectedly calls can_change_pte_writable"
		[[ "$PROBE_CAN_CHANGE_CALLS" -ge 1 ]] ||
			fail "probe machine code lacks the direct single-PTE writable check"
		;;
	folio-only)
		[[ "$CHILD_CAN_CHANGE_CALLS" -eq 0 && "$PROBE_CAN_CHANGE_CALLS" -ge 1 ]] ||
			fail "folio-only probe lacks the expected direct single-PTE path"
		[[ "$PROBE_VM_NORMAL_CALLS" -eq "$CHILD_VM_NORMAL_CALLS" ]] ||
			fail "folio-only probe did not preserve vm_normal_folio call count"
		;;
	no-folio)
		[[ "$CHILD_CAN_CHANGE_CALLS" -eq 0 && "$PROBE_CAN_CHANGE_CALLS" -ge 1 ]] ||
			fail "no-folio probe lacks the expected direct single-PTE path"
		[[ $((CHILD_VM_NORMAL_CALLS - PROBE_VM_NORMAL_CALLS)) -eq 1 ]] ||
			fail "no-folio probe did not remove exactly one vm_normal_folio call"
		;;
	*)
		fail "unknown machine-code audit gate: $AUDIT_GATE"
		;;
esac

cat > "$BASE/build-logs/probe.artifacts.tsv" <<EOF
role	probe
commit	$CHILD_COMMIT
diagnostic	$DIAGNOSTIC
kernelrelease	$KERNELRELEASE
config_sha256	$(sha256sum "$SOURCE/.config" | awk '{ print $1 }')
canonical_config_sha256	$(canonical_config_sha256 "$SOURCE/.config")
bzimage_sha256	$(sha256sum "$SOURCE/arch/x86/boot/bzImage" | awk '{ print $1 }')
vmlinux_sha256	$(sha256sum "$SOURCE/vmlinux" | awk '{ print $1 }')
system_map_sha256	$(sha256sum "$SOURCE/System.map" | awk '{ print $1 }')
compiler	$(gcc --version | head -n1)
compiler_launcher	$COMPILER_LAUNCHER
module_signing_key_path	$SHARED_SIGNING_KEY
module_signing_pem_sha256	$(sha256sum "$SHARED_SIGNING_KEY" | awk '{ print $1 }')
module_signing_x509_sha256	$(sha256sum "$SOURCE/certs/signing_key.x509" | awk '{ print $1 }')
diagnostic_patch_sha256	$(sha256sum "$PATCH_FILE" | awk '{ print $1 }')
preemption_contract	dynamic-preempt-boot-none
required_kernel_cmdline	preempt=none
kbuild_timestamp	$KBUILD_BUILD_TIMESTAMP
kbuild_user	$KBUILD_BUILD_USER
kbuild_host	$KBUILD_BUILD_HOST
kbuild_version	$KBUILD_BUILD_VERSION
EOF

[[ $(manifest_value "$BASE/build-logs/probe.artifacts.tsv" canonical_config_sha256) == \
	$(manifest_value "$CHILD_ARTIFACT_MANIFEST" canonical_config_sha256) ]] ||
	fail "probe and child canonical configs differ"
[[ $(manifest_value "$BASE/build-logs/probe.artifacts.tsv" module_signing_pem_sha256) == \
	$(manifest_value "$CHILD_ARTIFACT_MANIFEST" module_signing_pem_sha256) ]] ||
	fail "probe and child module signing identities differ"
[[ $(manifest_value "$BASE/build-logs/probe.artifacts.tsv" module_signing_x509_sha256) == \
	$(manifest_value "$CHILD_ARTIFACT_MANIFEST" module_signing_x509_sha256) ]] ||
	fail "probe and child module signing certificates differ"

for path in \
	"/boot/vmlinuz-$KERNELRELEASE" \
	"/boot/System.map-$KERNELRELEASE" \
	"/boot/config-$KERNELRELEASE" \
	"/boot/initrd.img-$KERNELRELEASE" \
	"/lib/modules/$KERNELRELEASE"; do
	if sudo test -e "$path"; then
		fail "refusing previous/partial installation: $path exists"
	fi
done

INSTALL_LOG="$BASE/install-logs/probe.install.$(date -u +%Y%m%dT%H%M%SZ).log"
echo "installing mprotect diagnostic probe release=$KERNELRELEASE log=$INSTALL_LOG"
(
	cd "$SOURCE"
	sudo --preserve-env=KBUILD_BUILD_TIMESTAMP,KBUILD_BUILD_USER,KBUILD_BUILD_HOST,KBUILD_BUILD_VERSION \
		make modules_install
	sudo depmod "$KERNELRELEASE"
	sudo install -m 0644 arch/x86/boot/bzImage "/boot/vmlinuz-$KERNELRELEASE"
	sudo install -m 0644 System.map "/boot/System.map-$KERNELRELEASE"
	sudo install -m 0644 .config "/boot/config-$KERNELRELEASE"
	sudo update-initramfs -c -k "$KERNELRELEASE"
) 2>&1 | tee "$INSTALL_LOG"

[[ $(sha256sum "/boot/config-$KERNELRELEASE" | awk '{ print $1 }') == \
	$(manifest_value "$BASE/build-logs/probe.artifacts.tsv" config_sha256) ]] ||
	fail "installed probe config hash mismatch"
[[ $(sha256sum "/boot/vmlinuz-$KERNELRELEASE" | awk '{ print $1 }') == \
	$(manifest_value "$BASE/build-logs/probe.artifacts.tsv" bzimage_sha256) ]] ||
	fail "installed probe kernel hash mismatch"
sudo test -s "/boot/initrd.img-$KERNELRELEASE" || fail "probe initrd is missing"
sudo test -s "/lib/modules/$KERNELRELEASE/modules.dep" || fail "probe modules.dep is missing"

cat > "$BASE/install-logs/probe.installed.tsv" <<EOF
role	probe
kernelrelease	$KERNELRELEASE
config_sha256	$(sha256sum "/boot/config-$KERNELRELEASE" | awk '{ print $1 }')
vmlinuz_sha256	$(sha256sum "/boot/vmlinuz-$KERNELRELEASE" | awk '{ print $1 }')
initrd_sha256	$(sudo sha256sum "/boot/initrd.img-$KERNELRELEASE" | awk '{ print $1 }')
modules_dep_sha256	$(sha256sum "/lib/modules/$KERNELRELEASE/modules.dep" | awk '{ print $1 }')
EOF

cp "$PARENT_ARTIFACT_MANIFEST" "$BASE/build-logs/parent.artifacts.tsv"
cp "$CHILD_ARTIFACT_MANIFEST" "$BASE/build-logs/child.artifacts.tsv"
cp "$EXACT_BASE/workload/mprotect_shared_dirty_reproducer.c" "$BASE/workload/"
sudo update-grub
sudo grep -Fq 'set default="Advanced options for Ubuntu>Ubuntu, with Linux 7.0.0-27-generic"' \
	/boot/grub/grub.cfg || fail "rescue kernel is no longer the GRUB default"

cat > "$BASE/build-logs/matrix-status.env" <<EOF
status	complete
parent_commit	$(manifest_value "$PARENT_ARTIFACT_MANIFEST" commit)
child_commit	$CHILD_COMMIT
probe_base_commit	$CHILD_COMMIT
parent_release	$(manifest_value "$PARENT_ARTIFACT_MANIFEST" kernelrelease)
child_release	$CHILD_RELEASE
probe_release	$KERNELRELEASE
canonical_config_sha256	$EXACT_CANONICAL
diagnostic_patch_sha256	$(sha256sum "$PATCH_FILE" | awk '{ print $1 }')
required_kernel_cmdline	preempt=none
completed_utc	$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

cat "$BASE/build-logs/machine-code-audit.tsv"
cat "$BASE/build-logs/matrix-status.env"
df -h / /boot/efi
