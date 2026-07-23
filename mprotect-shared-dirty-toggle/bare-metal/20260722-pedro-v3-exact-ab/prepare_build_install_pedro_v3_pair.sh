#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOT=${ROOT:-/home/lcf/kernel-study}
BASE=${BASE:-$ROOT/linux-baremetal/mprotect-pedro-v3-exact-20260722}
CONFIG_SEED=${CONFIG_SEED:-/boot/config-7.1.3-bm-7.1.3}
JOBS=${JOBS:-$(nproc)}
KBUILD_BUILD_TIMESTAMP=${KBUILD_BUILD_TIMESTAMP:-2026-07-22 00:00:00 UTC}
KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-kernel-study}
KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-linux-perf-bm}
KBUILD_BUILD_VERSION=${KBUILD_BUILD_VERSION:-1}
SHARED_SIGNING_KEY=${SHARED_SIGNING_KEY:-$BASE/keys/mprotect-pedro-v3-signing-key.pem}

# v7.1.3 already contains Pedro's complete v3 two-patch series. No later
# mm/mprotect.c change exists between the series tip and v7.1.3, so replacing
# only mm/mprotect.c with the exact pre-series blob constructs a matched
# v7.1.3 no-v3 control.
CURRENT_COMMIT=199c9959d3a9b53f346c221757fc7ac507fbac50
PRE_SERIES_COMMIT=19999e479c2a38672789e66b4830f43c645ca1f2
PATCH1_COMMIT=3bc181c1436373e42220baaa0d8c9b45fa18afe1
PATCH2_COMMIT=89e613bc0b2d6d4a18a09b161131ce4ca5c70f2a
CURRENT_MPROTECT_SHA256=79083647ff1f763c7e131d3ceba4bd9b313f9106e8d9a6b8a519acbf2a984906
PRE_SERIES_MPROTECT_SHA256=b189d27285e3506b090a6e3f242b9a9ec96ca913613ad084e745cdf51730f54f

PARENT_LOCALVERSION=-mprotect-pv3-base-19999e479c2a
CHILD_LOCALVERSION=-mprotect-pv3-full-89e613bc0b2d

mkdir -p \
	"$BASE/tarballs" \
	"$BASE/sources" \
	"$BASE/manifests" \
	"$BASE/prep-logs" \
	"$BASE/build-logs" \
	"$BASE/install-logs" \
	"$BASE/keys"

exec > >(tee "$BASE/prepare-build-install.$(date -u +%Y%m%dT%H%M%SZ).log") 2>&1

fail()
{
	echo "Pedro v3 pair preparation failed: $*" >&2
	exit 1
}

canonical_config_sha256()
{
	sed '/^CONFIG_LOCALVERSION=/d' "$1" | sha256sum | awk '{ print $1 }'
}

manifest_value()
{
	local manifest=$1
	local key=$2
	awk -F '\t' -v key="$key" '$1 == key { print $2; exit }' "$manifest"
}

prepare_shared_signing_key()
{
	local x509_config=$1

	if [[ ! -s "$SHARED_SIGNING_KEY" ]]; then
		[[ -s "$x509_config" ]] || fail "missing X.509 config $x509_config"
		echo "generating one shared module signing key outside both source trees"
		openssl req -new -nodes -utf8 -sha512 -days 36500 \
			-batch -x509 -config "$x509_config" \
			-outform PEM -out "$SHARED_SIGNING_KEY" \
			-keyout "$SHARED_SIGNING_KEY"
		chmod 0600 "$SHARED_SIGNING_KEY"
	fi

	[[ -s "$SHARED_SIGNING_KEY" ]] || fail "shared module signing key is missing"
	[[ $(stat -c '%a' "$SHARED_SIGNING_KEY") == 600 ]] ||
		fail "shared module signing key must have mode 0600"
}

download_sources()
{
	local tarball="$BASE/tarballs/linux-$CURRENT_COMMIT.tar.gz"
	local tarball_url="https://codeload.github.com/torvalds/linux/tar.gz/$CURRENT_COMMIT"
	local base_file="$BASE/tarballs/mm-mprotect-$PRE_SERIES_COMMIT.c"
	local base_file_url="https://raw.githubusercontent.com/torvalds/linux/$PRE_SERIES_COMMIT/mm/mprotect.c"
	local top

	if [[ ! -s "$tarball" ]]; then
		echo "downloading exact v7.1.3 source commit $CURRENT_COMMIT"
		curl -fL --retry 3 --retry-delay 2 --progress-bar \
			-o "$tarball.tmp" "$tarball_url"
		mv "$tarball.tmp" "$tarball"
	fi
	top=$(tar -tzf "$tarball" | sed -n '1s,/.*,,p')
	[[ "$top" == "linux-$CURRENT_COMMIT" ]] ||
		fail "archive top directory is $top, expected linux-$CURRENT_COMMIT"

	if [[ ! -s "$base_file" ]]; then
		echo "downloading exact pre-series mm/mprotect.c blob $PRE_SERIES_COMMIT"
		curl -fL --retry 3 --retry-delay 2 --progress-bar \
			-o "$base_file.tmp" "$base_file_url"
		mv "$base_file.tmp" "$base_file"
	fi
	[[ $(sha256sum "$base_file" | awk '{ print $1 }') == "$PRE_SERIES_MPROTECT_SHA256" ]] ||
		fail "pre-series mm/mprotect.c hash mismatch"
}

extract_role()
{
	local role=$1
	local source="$BASE/sources/linux-$role"
	local tarball="$BASE/tarballs/linux-$CURRENT_COMMIT.tar.gz"

	if [[ ! -d "$source" ]]; then
		mkdir "$source"
		tar -xzf "$tarball" -C "$source" --strip-components=1
	fi
	[[ -s "$source/mm/mprotect.c" ]] || fail "missing mm/mprotect.c for $role"
}

prepare_source_identity()
{
	local parent_source="$BASE/sources/linux-parent"
	local child_source="$BASE/sources/linux-child"
	local base_file="$BASE/tarballs/mm-mprotect-$PRE_SERIES_COMMIT.c"
	local diff_audit="$BASE/manifests/source-tree-diff-before-config.txt"

	[[ $(sha256sum "$child_source/mm/mprotect.c" | awk '{ print $1 }') == \
		"$CURRENT_MPROTECT_SHA256" ]] || fail "v7.1.3 mprotect hash differs from v3 tip"
	cp "$base_file" "$parent_source/mm/mprotect.c"
	[[ $(sha256sum "$parent_source/mm/mprotect.c" | awk '{ print $1 }') == \
		"$PRE_SERIES_MPROTECT_SHA256" ]] || fail "constructed no-v3 mprotect hash mismatch"

	set +e
	diff -qr "$parent_source" "$child_source" > "$diff_audit"
	local diff_rc=$?
	set -e
	[[ $diff_rc -eq 1 ]] || fail "expected exactly one source difference, diff rc=$diff_rc"
	[[ $(wc -l < "$diff_audit") -eq 1 ]] || fail "source trees differ in more than one path"
	grep -Eq '^Files .*/mm/mprotect\.c and .*/mm/mprotect\.c differ$' "$diff_audit" ||
		fail "source-tree difference is not limited to mm/mprotect.c"
}

prepare_role_config()
{
	local role=$1
	local localversion=$2
	local series_state=$3
	local mprotect_commit=$4
	local source="$BASE/sources/linux-$role"
	local tarball="$BASE/tarballs/linux-$CURRENT_COMMIT.tar.gz"
	local url="https://codeload.github.com/torvalds/linux/tar.gz/$CURRENT_COMMIT"

	[[ -s "$CONFIG_SEED" ]] || fail "missing config seed $CONFIG_SEED"
	prepare_shared_signing_key "$source/certs/default_x509.genkey"
	cp "$CONFIG_SEED" "$source/.config"

	(
		cd "$source"
		scripts/config --set-str LOCALVERSION "$localversion"
		scripts/config --disable LOCALVERSION_AUTO
		scripts/config --disable WERROR || true
		scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
		scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
		scripts/config --set-str BUILD_SALT ""
		scripts/config --set-str MODULE_SIG_KEY "$SHARED_SIGNING_KEY"

		scripts/config --disable PREEMPT_NONE || true
		scripts/config --disable PREEMPT_VOLUNTARY || true
		scripts/config --disable PREEMPT_LAZY || true
		scripts/config --disable PREEMPT_RT || true
		scripts/config --enable PREEMPT
		scripts/config --enable PREEMPT_DYNAMIC

		scripts/config --disable MODVERSIONS || true
		scripts/config --disable BASIC_MODVERSIONS || true
		scripts/config --disable EXTENDED_MODVERSIONS || true
		scripts/config --disable GENDWARFKSYMS || true
		scripts/config --disable DEBUG_INFO || true
		scripts/config --disable DEBUG_INFO_BTF || true
		scripts/config --disable DEBUG_INFO_BTF_MODULES || true
		scripts/config --disable DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT || true
		scripts/config --disable DEBUG_INFO_DWARF4 || true
		scripts/config --disable DEBUG_INFO_DWARF5 || true
		scripts/config --enable DEBUG_INFO_NONE || true

		make olddefconfig

		grep -qx 'CONFIG_PREEMPT=y' .config
		grep -qx 'CONFIG_PREEMPT_DYNAMIC=y' .config
		grep -qx 'CONFIG_HAVE_PREEMPT_DYNAMIC=y' .config
		if grep -q '^CONFIG_PREEMPT_LAZY=y' .config; then
			fail "$role unexpectedly enables CONFIG_PREEMPT_LAZY"
		fi
		if grep -q '^CONFIG_PREEMPT_NONE=y' .config; then
			fail "$role unexpectedly enables CONFIG_PREEMPT_NONE"
		fi
		grep -qx 'CONFIG_DEBUG_INFO_NONE=y' .config
		grep -qx 'CONFIG_SYSTEM_TRUSTED_KEYS=""' .config
		grep -qx 'CONFIG_SYSTEM_REVOCATION_KEYS=""' .config
		grep -Fqx "CONFIG_MODULE_SIG_KEY=\"$SHARED_SIGNING_KEY\"" .config
	)

	make -C "$source" clean
	rm -f "$source/certs/signing_key.pem" "$source/certs/signing_key.x509"

	local kernelversion
	local kernelrelease
	kernelversion=$(make -s -C "$source" kernelversion)
	kernelrelease=$(make -s -C "$source" kernelrelease)
	[[ "$kernelversion" == 7.1.3 ]] || fail "$role kernelversion is $kernelversion"

	cat > "$BASE/manifests/$role.source.tsv" <<EOF
role	$role
source_commit	$CURRENT_COMMIT
source_tag	v7.1.3
series_base_commit	$PRE_SERIES_COMMIT
series_patch1_commit	$PATCH1_COMMIT
series_patch2_commit	$PATCH2_COMMIT
series_state	$series_state
mm_mprotect_blob_commit	$mprotect_commit
constructed_no_series	$([[ "$series_state" == off ]] && echo yes || echo no)
archive_url	$url
archive_sha256	$(sha256sum "$tarball" | awk '{ print $1 }')
mm_mprotect_c_sha256	$(sha256sum "$source/mm/mprotect.c" | awk '{ print $1 }')
config_seed	$CONFIG_SEED
config_seed_sha256	$(sha256sum "$CONFIG_SEED" | awk '{ print $1 }')
config_sha256	$(sha256sum "$source/.config" | awk '{ print $1 }')
canonical_config_sha256	$(canonical_config_sha256 "$source/.config")
kernelversion	$kernelversion
kernelrelease	$kernelrelease
localversion	$localversion
preemption_contract	dynamic-preempt-boot-none
required_kernel_cmdline	preempt=none
EOF

	echo "prepared role=$role series_state=$series_state kernelrelease=$kernelrelease"
}

build_role()
{
	local role=$1
	local source="$BASE/sources/linux-$role"
	local source_manifest="$BASE/manifests/$role.source.tsv"
	local log
	log="$BASE/build-logs/$role.build.$(date -u +%Y%m%dT%H%M%SZ).log"
	local compiler_launcher=none
	local -a make_args=(-j"$JOBS")

	if command -v ccache >/dev/null 2>&1; then
		export CCACHE_DIR=${CCACHE_DIR:-$ROOT/linux-baremetal/ccache}
		export CCACHE_BASEDIR=$BASE/sources
		export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-20G}
		export CCACHE_NOHASHDIR=true
		export CCACHE_COMPRESS=true
		make_args+=(CC="ccache gcc")
		compiler_launcher=$(ccache --version | head -n1)
	fi

	export KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_USER KBUILD_BUILD_HOST KBUILD_BUILD_VERSION
	echo "building role=$role jobs=$JOBS log=$log"
	make -C "$source" "${make_args[@]}" 2>&1 | tee "$log"

	local kernelrelease
	local canonical_sha256
	kernelrelease=$(make -s -C "$source" kernelrelease)
	canonical_sha256=$(canonical_config_sha256 "$source/.config")
	[[ "$kernelrelease" == "$(manifest_value "$source_manifest" kernelrelease)" ]] ||
		fail "$role kernelrelease changed after build"
	[[ "$canonical_sha256" == "$(manifest_value "$source_manifest" canonical_config_sha256)" ]] ||
		fail "$role canonical config changed after build"
	[[ -s "$source/arch/x86/boot/bzImage" ]] || fail "$role bzImage is missing"
	[[ -s "$source/vmlinux" ]] || fail "$role vmlinux is missing"
	[[ -s "$source/System.map" ]] || fail "$role System.map is missing"

	cat > "$BASE/build-logs/$role.artifacts.tsv" <<EOF
role	$role
commit	$(manifest_value "$source_manifest" mm_mprotect_blob_commit)
source_commit	$CURRENT_COMMIT
series_state	$(manifest_value "$source_manifest" series_state)
kernelrelease	$kernelrelease
config_sha256	$(sha256sum "$source/.config" | awk '{ print $1 }')
canonical_config_sha256	$canonical_sha256
bzimage_sha256	$(sha256sum "$source/arch/x86/boot/bzImage" | awk '{ print $1 }')
vmlinux_sha256	$(sha256sum "$source/vmlinux" | awk '{ print $1 }')
system_map_sha256	$(sha256sum "$source/System.map" | awk '{ print $1 }')
compiler	$(gcc --version | head -n1)
compiler_launcher	$compiler_launcher
module_signing_key_path	$SHARED_SIGNING_KEY
module_signing_pem_sha256	$(sha256sum "$SHARED_SIGNING_KEY" | awk '{ print $1 }')
module_signing_x509_sha256	$(sha256sum "$source/certs/signing_key.x509" | awk '{ print $1 }')
preemption_contract	dynamic-preempt-boot-none
required_kernel_cmdline	preempt=none
kbuild_timestamp	$KBUILD_BUILD_TIMESTAMP
kbuild_user	$KBUILD_BUILD_USER
kbuild_host	$KBUILD_BUILD_HOST
kbuild_version	$KBUILD_BUILD_VERSION
EOF
}

install_role()
{
	local role=$1
	local source="$BASE/sources/linux-$role"
	local manifest="$BASE/build-logs/$role.artifacts.tsv"
	local release
	local log
	log="$BASE/install-logs/$role.install.$(date -u +%Y%m%dT%H%M%SZ).log"
	release=$(manifest_value "$manifest" kernelrelease)

	for path in \
		"/boot/vmlinuz-$release" \
		"/boot/System.map-$release" \
		"/boot/config-$release" \
		"/boot/initrd.img-$release" \
		"/lib/modules/$release"; do
		if sudo test -e "$path"; then
			fail "refusing partial/previous installation: $path already exists"
		fi
	done

	echo "installing role=$role release=$release log=$log"
	(
		export KBUILD_BUILD_TIMESTAMP KBUILD_BUILD_USER KBUILD_BUILD_HOST KBUILD_BUILD_VERSION
		cd "$source"
		sudo --preserve-env=KBUILD_BUILD_TIMESTAMP,KBUILD_BUILD_USER,KBUILD_BUILD_HOST,KBUILD_BUILD_VERSION \
			make modules_install
		sudo depmod "$release"
		sudo install -m 0644 arch/x86/boot/bzImage "/boot/vmlinuz-$release"
		sudo install -m 0644 System.map "/boot/System.map-$release"
		sudo install -m 0644 .config "/boot/config-$release"
		sudo update-initramfs -c -k "$release"
	) 2>&1 | tee "$log"

	[[ "$(sha256sum "/boot/config-$release" | awk '{ print $1 }')" == \
		"$(manifest_value "$manifest" config_sha256)" ]] || fail "$role installed config hash mismatch"
	[[ "$(sha256sum "/boot/vmlinuz-$release" | awk '{ print $1 }')" == \
		"$(manifest_value "$manifest" bzimage_sha256)" ]] || fail "$role installed kernel hash mismatch"
	sudo test -s "/boot/initrd.img-$release" || fail "$role initrd is missing"
	sudo test -s "/lib/modules/$release/modules.dep" || fail "$role modules.dep is missing"

	cat > "$BASE/install-logs/$role.installed.tsv" <<EOF
role	$role
kernelrelease	$release
config_sha256	$(sha256sum "/boot/config-$release" | awk '{ print $1 }')
vmlinuz_sha256	$(sha256sum "/boot/vmlinuz-$release" | awk '{ print $1 }')
initrd_sha256	$(sudo sha256sum "/boot/initrd.img-$release" | awk '{ print $1 }')
modules_dep_sha256	$(sha256sum "/lib/modules/$release/modules.dep" | awk '{ print $1 }')
EOF
}

sudo -n true || fail "passwordless sudo is required"
[[ $(uname -r) == 7.0.0-27-generic ]] || fail "build/install must start from rescue kernel"
[[ -z $(sudo efibootmgr | awk -F': ' '$1 == "BootNext" { print $2 }') ]] ||
	fail "another BootNext is pending"
[[ ${#PARENT_LOCALVERSION} -eq ${#CHILD_LOCALVERSION} ]] ||
	fail "localversion strings are not equal length"

download_sources
extract_role parent
extract_role child
prepare_source_identity
prepare_role_config parent "$PARENT_LOCALVERSION" off "$PRE_SERIES_COMMIT"
prepare_role_config child "$CHILD_LOCALVERSION" on "$PATCH2_COMMIT"

PARENT_CANONICAL=$(manifest_value "$BASE/manifests/parent.source.tsv" canonical_config_sha256)
CHILD_CANONICAL=$(manifest_value "$BASE/manifests/child.source.tsv" canonical_config_sha256)
[[ "$PARENT_CANONICAL" == "$CHILD_CANONICAL" ]] || {
	diff -u \
		<(sed '/^CONFIG_LOCALVERSION=/d' "$BASE/sources/linux-parent/.config") \
		<(sed '/^CONFIG_LOCALVERSION=/d' "$BASE/sources/linux-child/.config") || true
	fail "parent and child canonical configs differ"
}

build_role parent
build_role child

PARENT_CANONICAL=$(manifest_value "$BASE/build-logs/parent.artifacts.tsv" canonical_config_sha256)
CHILD_CANONICAL=$(manifest_value "$BASE/build-logs/child.artifacts.tsv" canonical_config_sha256)
[[ "$PARENT_CANONICAL" == "$CHILD_CANONICAL" ]] || fail "built canonical configs differ"
[[ "$(manifest_value "$BASE/build-logs/parent.artifacts.tsv" module_signing_pem_sha256)" == \
	"$(manifest_value "$BASE/build-logs/child.artifacts.tsv" module_signing_pem_sha256)" ]] ||
	fail "module signing keys differ"
[[ "$(manifest_value "$BASE/build-logs/parent.artifacts.tsv" module_signing_x509_sha256)" == \
	"$(manifest_value "$BASE/build-logs/child.artifacts.tsv" module_signing_x509_sha256)" ]] ||
	fail "module signing certificates differ"

install_role parent
install_role child
sudo update-grub

sudo grep -Fq 'set default="Advanced options for Ubuntu>Ubuntu, with Linux 7.0.0-27-generic"' \
	/boot/grub/grub.cfg || fail "rescue kernel is no longer the GRUB default"

cat > "$BASE/build-logs/matrix-status.env" <<EOF
status=complete
source_commit=$CURRENT_COMMIT
series_base_commit=$PRE_SERIES_COMMIT
series_patch1_commit=$PATCH1_COMMIT
series_patch2_commit=$PATCH2_COMMIT
parent_series_state=off
child_series_state=on
parent_release=$(manifest_value "$BASE/build-logs/parent.artifacts.tsv" kernelrelease)
child_release=$(manifest_value "$BASE/build-logs/child.artifacts.tsv" kernelrelease)
canonical_config_sha256=$PARENT_CANONICAL
required_kernel_cmdline=preempt=none
completed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

cat "$BASE/build-logs/matrix-status.env"
df -h / /boot/efi
