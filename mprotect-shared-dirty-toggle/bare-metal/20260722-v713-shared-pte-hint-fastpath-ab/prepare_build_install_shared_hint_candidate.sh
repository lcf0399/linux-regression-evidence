#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
ROOT=${ROOT:-/home/lcf/kernel-study}
EXACT_BASE=${EXACT_BASE:-$ROOT/linux-baremetal/mprotect-pedro-v3-exact-20260722}
BASE=${BASE:-$ROOT/linux-baremetal/mprotect-v713-shared-hint-20260722}
PATCH_FILE=${PATCH_FILE:-$SCRIPT_DIR/0001-RFC-mm-mprotect-avoid-shared-folio-lookup-without-batch-hint.patch}
REPRODUCER_SOURCE=${REPRODUCER_SOURCE:-$ROOT/linux-regression-evidence/mprotect-shared-dirty-toggle/reproducer/mprotect_shared_dirty_reproducer.c}
JOBS=${JOBS:-$(nproc)}
KBUILD_BUILD_TIMESTAMP=${KBUILD_BUILD_TIMESTAMP:-2026-07-22 00:00:00 UTC}
KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-kernel-study}
KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-linux-perf-bm}
KBUILD_BUILD_VERSION=${KBUILD_BUILD_VERSION:-1}

SOURCE_COMMIT=199c9959d3a9b53f346c221757fc7ac507fbac50
BASELINE_MPROTECT_SHA256=79083647ff1f763c7e131d3ceba4bd9b313f9106e8d9a6b8a519acbf2a984906
BASELINE_LOCALVERSION=-mprotect-pv3-full-89e613bc0b2d
CANDIDATE_LOCALVERSION=-mprotect-hint-one-000000000000

SOURCE_ARCHIVE="$EXACT_BASE/tarballs/linux-$SOURCE_COMMIT.tar.gz"
BASELINE_SOURCE="$EXACT_BASE/sources/linux-child"
BASELINE_CONFIG="$BASELINE_SOURCE/.config"
BASELINE_SOURCE_MANIFEST="$EXACT_BASE/source-manifests/child.source.tsv"
BASELINE_ARTIFACT="$EXACT_BASE/build-logs/child.artifacts.tsv"
SHARED_SIGNING_KEY="$EXACT_BASE/keys/mprotect-pedro-v3-signing-key.pem"
CANDIDATE_SOURCE="$BASE/sources/linux-candidate"

mkdir -p "$BASE"/{sources,manifests,prep-logs,build-logs,install-logs,workload}
exec > >(tee "$BASE/prepare-build-install.$(date -u +%Y%m%dT%H%M%SZ).log") 2>&1

fail()
{
	echo "shared-hint candidate preparation failed: $*" >&2
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

sudo -n true || fail "passwordless sudo is required"
[[ $(uname -r) == 7.0.0-27-generic ]] ||
	fail "build/install must start from the 7.0.0-27-generic rescue kernel"
[[ -z $(sudo efibootmgr | awk -F': ' '$1 == "BootNext" { print $2 }') ]] ||
	fail "another BootNext is pending"
[[ ${#BASELINE_LOCALVERSION} -eq ${#CANDIDATE_LOCALVERSION} ]] ||
	fail "baseline and candidate localversion strings differ in length"

for path in "$SOURCE_ARCHIVE" "$BASELINE_CONFIG" "$BASELINE_SOURCE_MANIFEST" \
	"$BASELINE_ARTIFACT" \
	"$BASELINE_SOURCE/vmlinux" "$SHARED_SIGNING_KEY" "$PATCH_FILE" \
	"$REPRODUCER_SOURCE"; do
	[[ -s "$path" ]] || fail "missing prerequisite $path"
done

[[ $(manifest_value "$BASELINE_ARTIFACT" kernelrelease) == \
	"7.1.3$BASELINE_LOCALVERSION" ]] || fail "unexpected baseline release"
[[ $(sha256sum "$BASELINE_SOURCE/mm/mprotect.c" | awk '{ print $1 }') == \
	"$BASELINE_MPROTECT_SHA256" ]] || fail "baseline mm/mprotect.c hash mismatch"
[[ $(sha256sum "$BASELINE_CONFIG" | awk '{ print $1 }') == \
	$(manifest_value "$BASELINE_ARTIFACT" config_sha256) ]] ||
	fail "baseline config hash mismatch"
[[ $(sha256sum "$SHARED_SIGNING_KEY" | awk '{ print $1 }') == \
	$(manifest_value "$BASELINE_ARTIFACT" module_signing_pem_sha256) ]] ||
	fail "shared signing key hash mismatch"
[[ $(gcc --version | head -n1) == $(manifest_value "$BASELINE_ARTIFACT" compiler) ]] ||
	fail "compiler differs from the exact v7.1.3 baseline build"

mapfile -t PATCH_TARGETS < <(sed -n 's,^+++ b/,,p' "$PATCH_FILE")
[[ ${#PATCH_TARGETS[@]} -eq 1 && ${PATCH_TARGETS[0]} == mm/mprotect.c ]] ||
	fail "candidate patch must target only mm/mprotect.c"

[[ ! -e "$CANDIDATE_SOURCE" ]] || fail "source directory already exists: $CANDIDATE_SOURCE"
mkdir "$CANDIDATE_SOURCE"
TOP=$(tar -tzf "$SOURCE_ARCHIVE" | sed -n '1s,/.*,,p')
[[ "$TOP" == "linux-$SOURCE_COMMIT" ]] ||
	fail "archive top directory is $TOP, expected linux-$SOURCE_COMMIT"
tar -xzf "$SOURCE_ARCHIVE" -C "$CANDIDATE_SOURCE" --strip-components=1

BEFORE_MPROTECT_SHA256=$(sha256sum "$CANDIDATE_SOURCE/mm/mprotect.c" | awk '{ print $1 }')
[[ "$BEFORE_MPROTECT_SHA256" == "$BASELINE_MPROTECT_SHA256" ]] ||
	fail "extracted baseline mm/mprotect.c hash mismatch"
(
	cd "$CANDIDATE_SOURCE"
	patch --dry-run -p1 < "$PATCH_FILE"
	patch -p1 < "$PATCH_FILE"
	patch --reverse --dry-run -p1 < "$PATCH_FILE"
)
grep -Fq 'pte_batch_hint(pte, oldpte) == 1' "$CANDIDATE_SOURCE/mm/mprotect.c" ||
	fail "candidate source marker is absent"
AFTER_MPROTECT_SHA256=$(sha256sum "$CANDIDATE_SOURCE/mm/mprotect.c" | awk '{ print $1 }')
[[ "$AFTER_MPROTECT_SHA256" != "$BEFORE_MPROTECT_SHA256" ]] ||
	fail "candidate patch did not change mm/mprotect.c"

cp "$BASELINE_CONFIG" "$CANDIDATE_SOURCE/.config"
(
	cd "$CANDIDATE_SOURCE"
	scripts/config --set-str LOCALVERSION "$CANDIDATE_LOCALVERSION"
	scripts/config --disable LOCALVERSION_AUTO
	make olddefconfig
)

BASELINE_CANONICAL=$(manifest_value "$BASELINE_ARTIFACT" canonical_config_sha256)
CANDIDATE_CANONICAL=$(canonical_config_sha256 "$CANDIDATE_SOURCE/.config")
[[ "$CANDIDATE_CANONICAL" == "$BASELINE_CANONICAL" ]] || {
	diff -u \
		<(sed '/^CONFIG_LOCALVERSION=/d' "$BASELINE_CONFIG") \
		<(sed '/^CONFIG_LOCALVERSION=/d' "$CANDIDATE_SOURCE/.config") || true
	fail "candidate canonical config differs from exact baseline"
}
grep -qx 'CONFIG_PREEMPT=y' "$CANDIDATE_SOURCE/.config"
grep -qx 'CONFIG_PREEMPT_DYNAMIC=y' "$CANDIDATE_SOURCE/.config"
grep -Fqx "CONFIG_MODULE_SIG_KEY=\"$SHARED_SIGNING_KEY\"" "$CANDIDATE_SOURCE/.config"

make -C "$CANDIDATE_SOURCE" clean
rm -f "$CANDIDATE_SOURCE/certs/signing_key.pem" "$CANDIDATE_SOURCE/certs/signing_key.x509"

KERNELVERSION=$(make -s -C "$CANDIDATE_SOURCE" kernelversion)
KERNELRELEASE=$(make -s -C "$CANDIDATE_SOURCE" kernelrelease)
BASELINE_RELEASE=$(manifest_value "$BASELINE_ARTIFACT" kernelrelease)
[[ "$KERNELVERSION" == 7.1.3 ]] || fail "candidate kernelversion is $KERNELVERSION"
[[ ${#KERNELRELEASE} -eq ${#BASELINE_RELEASE} ]] ||
	fail "candidate and baseline release strings differ in length"

cat > "$BASE/manifests/candidate.source.tsv" <<EOF
role	candidate
base_commit	$SOURCE_COMMIT
base_tag	v7.1.3
candidate_kind	shared-nonnuma-pte-hint-one-skip-page-lookup
archive_path	$SOURCE_ARCHIVE
archive_sha256	$(sha256sum "$SOURCE_ARCHIVE" | awk '{ print $1 }')
patch_path	$PATCH_FILE
patch_sha256	$(sha256sum "$PATCH_FILE" | awk '{ print $1 }')
patch_targets	mm/mprotect.c
before_mm_mprotect_c_sha256	$BEFORE_MPROTECT_SHA256
after_mm_mprotect_c_sha256	$AFTER_MPROTECT_SHA256
config_source	$BASELINE_CONFIG
config_sha256	$(sha256sum "$CANDIDATE_SOURCE/.config" | awk '{ print $1 }')
canonical_config_sha256	$CANDIDATE_CANONICAL
kernelversion	$KERNELVERSION
kernelrelease	$KERNELRELEASE
localversion	$CANDIDATE_LOCALVERSION
preemption_contract	dynamic-preempt-boot-none
required_kernel_cmdline	preempt=none
EOF

COMPILER_LAUNCHER=none
MAKE_ARGS=(-j"$JOBS")
if command -v ccache >/dev/null 2>&1; then
	export CCACHE_DIR=${CCACHE_DIR:-$ROOT/linux-baremetal/ccache}
	export CCACHE_BASEDIR=$ROOT/linux-baremetal
	export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-20G}
	export CCACHE_NOHASHDIR=true
	export CCACHE_COMPRESS=true
	MAKE_ARGS+=(CC="ccache gcc")
	COMPILER_LAUNCHER=$(ccache --version | head -n1)
fi

export KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_USER KBUILD_BUILD_HOST KBUILD_BUILD_VERSION
BUILD_LOG="$BASE/build-logs/candidate.build.$(date -u +%Y%m%dT%H%M%SZ).log"
echo "building candidate release=$KERNELRELEASE jobs=$JOBS log=$BUILD_LOG"
make -C "$CANDIDATE_SOURCE" "${MAKE_ARGS[@]}" 2>&1 | tee "$BUILD_LOG"

[[ $(make -s -C "$CANDIDATE_SOURCE" kernelrelease) == "$KERNELRELEASE" ]] ||
	fail "kernelrelease changed after build"
[[ $(canonical_config_sha256 "$CANDIDATE_SOURCE/.config") == "$BASELINE_CANONICAL" ]] ||
	fail "canonical config changed after build"
for path in arch/x86/boot/bzImage vmlinux System.map certs/signing_key.x509; do
	[[ -s "$CANDIDATE_SOURCE/$path" ]] || fail "missing built artifact $path"
done

objdump -dr --disassemble=change_pte_range "$BASELINE_SOURCE/vmlinux" \
	> "$BASE/build-logs/baseline.change_pte_range.objdump.txt"
objdump -dr --disassemble=change_pte_range "$CANDIDATE_SOURCE/vmlinux" \
	> "$BASE/build-logs/candidate.change_pte_range.objdump.txt"

printf 'role\tchange_pte_range_size_hex\tchange_pte_range_objdump_sha256\tvm_normal_page_symbol\tmprotect_folio_pte_batch_symbol\n' \
	> "$BASE/build-logs/machine-code-audit.tsv"
for role in baseline candidate; do
	case "$role" in
		baseline) image=$BASELINE_SOURCE/vmlinux ;;
		candidate) image=$CANDIDATE_SOURCE/vmlinux ;;
	esac
	printf '%s\t%s\t%s\t%s\t%s\n' \
		"$role" \
		"$(symbol_size_hex "$image" change_pte_range)" \
		"$(sha256sum "$BASE/build-logs/$role.change_pte_range.objdump.txt" | awk '{ print $1 }')" \
		"$(nm "$image" | awk '$3 == "vm_normal_page" { print $2; exit }')" \
		"$(nm "$image" | awk '$3 == "mprotect_folio_pte_batch" { print $2; exit }')" \
		>> "$BASE/build-logs/machine-code-audit.tsv"
done
[[ $(awk -F '\t' '$1 == "baseline" { print $3 }' "$BASE/build-logs/machine-code-audit.tsv") != \
	$(awk -F '\t' '$1 == "candidate" { print $3 }' "$BASE/build-logs/machine-code-audit.tsv") ]] ||
	fail "candidate did not change change_pte_range machine code"
awk -F '\t' 'NR > 1 && ($4 == "" || $5 == "") { bad++ } END { exit bad != 0 }' \
	"$BASE/build-logs/machine-code-audit.tsv" || fail "required trace symbols are absent"

cat > "$BASE/build-logs/candidate.artifacts.tsv" <<EOF
role	candidate
commit	$SOURCE_COMMIT
candidate_kind	shared-nonnuma-pte-hint-one-skip-page-lookup
kernelrelease	$KERNELRELEASE
config_sha256	$(sha256sum "$CANDIDATE_SOURCE/.config" | awk '{ print $1 }')
canonical_config_sha256	$(canonical_config_sha256 "$CANDIDATE_SOURCE/.config")
bzimage_sha256	$(sha256sum "$CANDIDATE_SOURCE/arch/x86/boot/bzImage" | awk '{ print $1 }')
vmlinux_sha256	$(sha256sum "$CANDIDATE_SOURCE/vmlinux" | awk '{ print $1 }')
system_map_sha256	$(sha256sum "$CANDIDATE_SOURCE/System.map" | awk '{ print $1 }')
compiler	$(gcc --version | head -n1)
compiler_launcher	$COMPILER_LAUNCHER
module_signing_key_path	$SHARED_SIGNING_KEY
module_signing_pem_sha256	$(sha256sum "$SHARED_SIGNING_KEY" | awk '{ print $1 }')
module_signing_x509_sha256	$(sha256sum "$CANDIDATE_SOURCE/certs/signing_key.x509" | awk '{ print $1 }')
candidate_patch_sha256	$(sha256sum "$PATCH_FILE" | awk '{ print $1 }')
preemption_contract	dynamic-preempt-boot-none
required_kernel_cmdline	preempt=none
kbuild_timestamp	$KBUILD_BUILD_TIMESTAMP
kbuild_user	$KBUILD_BUILD_USER
kbuild_host	$KBUILD_BUILD_HOST
kbuild_version	$KBUILD_BUILD_VERSION
EOF

[[ $(manifest_value "$BASE/build-logs/candidate.artifacts.tsv" canonical_config_sha256) == \
	$(manifest_value "$BASELINE_ARTIFACT" canonical_config_sha256) ]] ||
	fail "candidate and baseline canonical configs differ"
[[ $(manifest_value "$BASE/build-logs/candidate.artifacts.tsv" module_signing_pem_sha256) == \
	$(manifest_value "$BASELINE_ARTIFACT" module_signing_pem_sha256) ]] ||
	fail "candidate and baseline module signing keys differ"
[[ $(manifest_value "$BASE/build-logs/candidate.artifacts.tsv" module_signing_x509_sha256) == \
	$(manifest_value "$BASELINE_ARTIFACT" module_signing_x509_sha256) ]] ||
	fail "candidate and baseline module signing certificates differ"

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

INSTALL_LOG="$BASE/install-logs/candidate.install.$(date -u +%Y%m%dT%H%M%SZ).log"
echo "installing candidate release=$KERNELRELEASE log=$INSTALL_LOG"
(
	cd "$CANDIDATE_SOURCE"
	sudo --preserve-env=KBUILD_BUILD_TIMESTAMP,KBUILD_BUILD_USER,KBUILD_BUILD_HOST,KBUILD_BUILD_VERSION \
		make modules_install
	sudo depmod "$KERNELRELEASE"
	sudo install -m 0644 arch/x86/boot/bzImage "/boot/vmlinuz-$KERNELRELEASE"
	sudo install -m 0644 System.map "/boot/System.map-$KERNELRELEASE"
	sudo install -m 0644 .config "/boot/config-$KERNELRELEASE"
	sudo update-initramfs -c -k "$KERNELRELEASE"
) 2>&1 | tee "$INSTALL_LOG"

[[ $(sha256sum "/boot/config-$KERNELRELEASE" | awk '{ print $1 }') == \
	$(manifest_value "$BASE/build-logs/candidate.artifacts.tsv" config_sha256) ]] ||
	fail "installed candidate config hash mismatch"
[[ $(sha256sum "/boot/vmlinuz-$KERNELRELEASE" | awk '{ print $1 }') == \
	$(manifest_value "$BASE/build-logs/candidate.artifacts.tsv" bzimage_sha256) ]] ||
	fail "installed candidate kernel hash mismatch"
sudo test -s "/boot/initrd.img-$KERNELRELEASE" || fail "candidate initrd is missing"
sudo test -s "/lib/modules/$KERNELRELEASE/modules.dep" || fail "candidate modules.dep is missing"

cat > "$BASE/install-logs/candidate.installed.tsv" <<EOF
role	candidate
kernelrelease	$KERNELRELEASE
config_sha256	$(sha256sum "/boot/config-$KERNELRELEASE" | awk '{ print $1 }')
vmlinuz_sha256	$(sha256sum "/boot/vmlinuz-$KERNELRELEASE" | awk '{ print $1 }')
initrd_sha256	$(sudo sha256sum "/boot/initrd.img-$KERNELRELEASE" | awk '{ print $1 }')
modules_dep_sha256	$(sha256sum "/lib/modules/$KERNELRELEASE/modules.dep" | awk '{ print $1 }')
EOF

awk -F '\t' 'BEGIN { OFS="\t" } $1 == "role" { $2="baseline" } { print }' \
	"$BASELINE_ARTIFACT" > "$BASE/build-logs/baseline.artifacts.tsv"
awk -F '\t' 'BEGIN { OFS="\t" } $1 == "role" { $2="baseline" } { print }' \
	"$BASELINE_SOURCE_MANIFEST" > "$BASE/manifests/baseline.source.tsv"
install -m 0644 "$REPRODUCER_SOURCE" "$BASE/workload/mprotect_shared_dirty_reproducer.c"
sudo update-grub
sudo grep -Fq 'set default="Advanced options for Ubuntu>Ubuntu, with Linux 7.0.0-27-generic"' \
	/boot/grub/grub.cfg || fail "rescue kernel is no longer the GRUB default"

cat > "$BASE/build-logs/matrix-status.env" <<EOF
status	complete
source_commit	$SOURCE_COMMIT
baseline_release	$BASELINE_RELEASE
candidate_release	$KERNELRELEASE
canonical_config_sha256	$BASELINE_CANONICAL
candidate_patch_sha256	$(sha256sum "$PATCH_FILE" | awk '{ print $1 }')
required_kernel_cmdline	preempt=none
completed_utc	$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

cat "$BASE/build-logs/machine-code-audit.tsv"
cat "$BASE/build-logs/matrix-status.env"
df -h / /boot/efi
