#!/bin/bash
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Builds the signed package that converts the factory partition table to
# the LineageOS layout (repartition-binary), to install once from the
# recovery before the first LineageOS install.
#
#   repartition-zip.sh <output-zip>
#
# Variables: TOP (the LineageOS tree), KEY (default
# $TOP/vendor/lineage-priv/keys/releasekey).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
TOP=${TOP:-${ANDROID_BUILD_TOP:?run source build/envsetup.sh first, or set TOP}}
KEY=${KEY:-$TOP/vendor/lineage-priv/keys/releasekey}
HOSTOUT=${HOSTOUT:-$TOP/out/host/linux-x86}
ZIP=$(realpath -m "${1:?output zip}")
for f in "$KEY.pk8" "$KEY.x509.pem" "$HOSTOUT/framework/signapk.jar"; do
	[ -f "$f" ] || { echo "repartition-zip: missing $f" >&2; exit 1; }
done

S=$(mktemp -d); trap 'rm -rf "$S"' EXIT
mkdir -p "$S/META-INF/com/google/android" "$S/gtaxl"
echo "# the installer is update-binary" > "$S/META-INF/com/google/android/updater-script"
cp "$HERE/repartition-binary" "$S/META-INF/com/google/android/update-binary"
for tool in "$HERE/gtaxl-gpt.c" "$HERE/../revert/gtaxl-dm.c"; do
	out="$S/gtaxl/$(basename "$tool" .c)"
	aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -o "$out" "$tool"
	aarch64-linux-gnu-strip "$out"
done

rm -f "$ZIP" "$ZIP.unsigned"
( cd "$S" && zip -q -r "$ZIP.unsigned" META-INF gtaxl )
java -Xmx4g -Djava.library.path="$HOSTOUT/lib64" -jar "$HOSTOUT/framework/signapk.jar" \
	-w "$KEY.x509.pem" "$KEY.pk8" "$ZIP.unsigned" "$ZIP"
rm -f "$ZIP.unsigned"
echo "$ZIP: $(stat -c %s "$ZIP") bytes"
