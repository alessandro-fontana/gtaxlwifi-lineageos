#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Builds the files of a release from the current build, as LineageOS's build
# servers do: target files signed with sign_target_files_apks, then the
# install package from ota_from_target_files. Run it after
# `m target-files-package otatools`.
#
#   build-release.sh <output-dir>
#
# Output:
#   gtaxlwifi-boot.img                BOOT: the loader with kernel, DTB, ramdisk
#   gtaxlwifi-recovery.img            RECOVERY: the loader with the recovery
#   gtaxlwifi-los23-install.zip       install and update, signed
#   gtaxlwifi-revert-to-factory.zip   back to the factory partition table
#   gtaxlwifi-odin-AP.tar.md5         both images for Odin's AP slot
#   SHA256SUMS
#
# Refuses target files older than the device tree, and images carrying
# files no package declares: modules built by hand with `m <module>` stay in
# the staging directories, which the images are made from.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=${TOP:-${ANDROID_BUILD_TOP:?run source build/envsetup.sh first, or set TOP}}
OUT=${OUT:-$TOP/out/target/product/gtaxlwifi}
BUILT_DEV=${BUILT_DEV:-$TOP/device/samsung/gtaxlwifi}
KEYS=${KEYS:-vendor/lineage-priv/keys}
# Next to a device tree (the development repository) that tree must be the
# one the build used; elsewhere the build's own device tree is checked.
DEV=$(cd "$HERE/.." && pwd)
[ -f "$DEV/device.mk" ] || DEV=$BUILT_DEV
R=$(realpath -m "${1:?output dir}")
die() { echo "build-release: $*" >&2; exit 1; }

# the device tree the build used is the repository's
if [ "$DEV" != "$BUILT_DEV" ] && diff -rq "$DEV" "$BUILT_DEV" | grep -vE '\.patches|boot-cmdline|firmware|proprietary|keys|tools' | grep -q .; then
	diff -rq "$DEV" "$BUILT_DEV" | grep -vE '\.patches|boot-cmdline|firmware|proprietary|keys|tools' >&2
	die "the device tree differs from the one the build used: copy it, then rebuild"
fi
TF=$OUT/obj/PACKAGING/target_files_intermediates/lineage_gtaxlwifi-target_files.zip
[ -f "$TF" ] || die "no target files: m target-files-package otatools"
# the target files are newer than every file of the device tree
NEWER=$(find "$BUILT_DEV" -type f -newer "$TF" | head -3)
[ -z "$NEWER" ] || die "target files older than $NEWER: m target-files-package otatools"

# The device's test apps and tools, and any gtaxl-* file the device tree
# does not name, stop the release.
mapfile -t TESTS < <(cat "$DEV"/tests/*/Android.bp 2>/dev/null | sed -n 's/^[[:space:]]*name: "\(.*\)",/\1/p')
DECLARED=$(find "$DEV" \( -name '*.mk' -o -name Android.bp -o -name '*.rc' -o -name '*.xml' \) \
	-not -path "$DEV/tests/*" -not -path "$DEV/tools/*" -exec cat {} +)
STRAY=$(unzip -Z1 "$TF" | grep -E '^(SYSTEM|SYSTEM_EXT|PRODUCT|VENDOR|ODM)/[^/].*[^/]$' | while read -r f; do
	b=${f##*/}
	for t in "${TESTS[@]}"; do
		[ "$b" = "$t" ] || [ "$b" = "$t.apk" ] && { echo "$f"; continue 2; }
	done
	case $b in gtaxl-*) grep -qF -- "$b" <<<"$DECLARED" || echo "$f";; esac
done)
[ -z "$STRAY" ] || { echo "$STRAY" >&2; die "files no package declares: m installclean, then m target-files-package otatools"; }

rm -rf "$R"; mkdir -p "$R"
T=$(mktemp -d "${TMPDIR:-/tmp}/gtaxl-release.XXXXXX"); trap 'rm -rf "$T"' EXIT

# as on the wiki's signing page (signing_builds), with this build's keys;
# PATH and the releasetools come from the tree's host output
export PATH=$TOP/out/host/linux-x86/bin:$PATH
( cd "$TOP" && sign_target_files_apks -o -d "$KEYS" "$TF" "$T/signed-target_files.zip" ) \
	> "$R/sign.log" 2>&1 || { tail -20 "$R/sign.log" >&2; die "sign_target_files_apks failed, see $R/sign.log"; }
( cd "$TOP" && ota_from_target_files -k "$KEYS/releasekey" --block --backup=true \
	"$T/signed-target_files.zip" "$R/gtaxlwifi-los23-install.zip" ) \
	> "$R/ota.log" 2>&1 || { tail -20 "$R/ota.log" >&2; die "ota_from_target_files failed, see $R/ota.log"; }
unzip -p "$T/signed-target_files.zip" IMAGES/recovery.img > "$R/gtaxlwifi-recovery.img"
unzip -p "$T/signed-target_files.zip" IMAGES/boot.img > "$R/gtaxlwifi-boot.img"
TOP="$TOP" DEVICE="$BUILT_DEV" KEY="$TOP/$KEYS/releasekey" \
	"$HERE/revert/revert-zip.sh" "$R/gtaxlwifi-revert-to-factory.zip" >/dev/null

# Odin: one tar for the AP slot, flashing BOOT and RECOVERY together. Plain
# .img entries named after the PIT partitions, ustar, and the md5 line Odin
# checks appended at the end, as in Samsung's own AP_*.tar.md5.
cp "$R/gtaxlwifi-boot.img" "$T/boot.img"
cp "$R/gtaxlwifi-recovery.img" "$T/recovery.img"
( cd "$T" && tar -H ustar --owner=0 --group=0 -cf gtaxlwifi-odin-AP.tar boot.img recovery.img \
	&& md5sum -t gtaxlwifi-odin-AP.tar >> gtaxlwifi-odin-AP.tar )
mv "$T/gtaxlwifi-odin-AP.tar" "$R/gtaxlwifi-odin-AP.tar.md5"

( cd "$R" && sha256sum gtaxlwifi-boot.img gtaxlwifi-recovery.img gtaxlwifi-los23-install.zip \
	gtaxlwifi-revert-to-factory.zip gtaxlwifi-odin-AP.tar.md5 > SHA256SUMS )
echo
grep -m1 '^ro.lineage.version=' "$OUT/product/etc/build.prop" || true
ls -l "$R"
cat "$R/SHA256SUMS"
