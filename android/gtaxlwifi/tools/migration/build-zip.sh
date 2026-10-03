#!/bin/bash
# Builds the install zip (update-binary next to this script) from the build
# in $OUT, the production U-Boot and the device tree's cmdline: super.img
# (lpmake of system, product, system_ext, vendor, vendor_dlkm, system_dlkm),
# boot.img (the ext4
# U-Boot reads) and u-boot.img, written by the recovery at fixed offsets. The
# partition table is written by U-Boot on the next boot (§65.10).
#
#   tools/migration/build-zip.sh <output-zip>
#   tools/migration/build-zip.sh --revert <output-zip>
#
# --revert builds the zip that restores the factory partition table
# (revert-binary): no images inside, only gtaxl-gpt.
#
# Variables: OUT (default out/target/product/gtaxlwifi), UBOOT (default
# /work/uboot/tools/u-boot.img.recovery), HOSTBIN (lpmake and lpdump, default the
# host tools of the same tree), KEY (default vendor/gtaxl-keys/releasekey:
# the zip is signed whole-file with signapk -w, as LineageOS signs its own
# packages; the recovery checks it against otacerts.zip, which holds this
# key's certificate). Install the zip with adb sideload or from microSD,
# never from internal storage (update-binary refuses).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
DEV=$(cd "$HERE/../.." && pwd)
TOP=${TOP:-${ANDROID_BUILD_TOP:-/work/aosp/lineage}}
OUT=${OUT:-$TOP/out/target/product/gtaxlwifi}
UBOOT=${UBOOT:-/work/uboot/tools/u-boot.img.recovery}
REVERT=0
[ "${1:-}" = --revert ] && { REVERT=1; shift; }
ZIP=$(realpath -m "${1:?output zip}")
K=$OUT/obj/KERNEL_OBJ/arch/arm64/boot
KEY=${KEY:-$TOP/vendor/gtaxl-keys/releasekey}
HOSTOUT=${HOSTOUT:-$TOP/out/host/linux-x86}
test -f "$KEY.pk8" && test -f "$KEY.x509.pem" && test -f "$HOSTOUT/framework/signapk.jar"

# zip the staging dir into $ZIP, signed with $KEY
make_zip() {
	rm -f "$ZIP" "$ZIP.unsigned"
	( cd "$S" && zip -q -r "$ZIP.unsigned" META-INF gtaxl )
	java -Xmx4g -Djava.library.path="$HOSTOUT/lib64" -jar "$HOSTOUT/framework/signapk.jar" \
		-w "$KEY.x509.pem" "$KEY.pk8" "$ZIP.unsigned" "$ZIP"
	rm -f "$ZIP.unsigned"
	echo "$ZIP: $(stat -c %s "$ZIP") bytes, signed with $(openssl x509 -in "$KEY.x509.pem" -noout -subject)"
}

S=$(mktemp -d); trap 'rm -rf "$S"' EXIT
mkdir -p "$S/META-INF/com/google/android" "$S/gtaxl/boot"
echo "# the installer is update-binary" > "$S/META-INF/com/google/android/updater-script"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -o "$S/gtaxl/gtaxl-gpt" "$HERE/../../ota/gtaxl-gpt.c"
aarch64-linux-gnu-strip "$S/gtaxl/gtaxl-gpt"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -o "$S/gtaxl/gtaxl-dm" "$HERE/gtaxl-dm.c"
aarch64-linux-gnu-strip "$S/gtaxl/gtaxl-dm"

if [ $REVERT = 1 ]; then
	cp "$HERE/revert-binary" "$S/META-INF/com/google/android/update-binary"
	make_zip
	exit 0
fi
# update-binary: the script, then gtaxl-unzip appended at HELPER_OFFSET (the
# recovery extracts only this file from a package the Updater app installs
# through a block map, §65.12). zlib from the tree, inflate only.
ZLIB=${ZLIB:-$TOP/external/zlib}
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -I"$ZLIB" -o "$S/gtaxl-unzip" "$HERE/gtaxl-unzip.c" \
	"$ZLIB"/{adler32,crc32,inffast,inflate,inftrees,zutil}.c
aarch64-linux-gnu-strip "$S/gtaxl-unzip"
UB=$S/META-INF/com/google/android/update-binary
grep -q '^HELPER_OFFSET=0000000000' "$HERE/update-binary"
N=$(( $(stat -c %s "$HERE/update-binary") + 1 ))
sed "s/^HELPER_OFFSET=0000000000/HELPER_OFFSET=$(printf '%010d' $N)/" "$HERE/update-binary" > "$UB"
test "$(stat -c %s "$UB")" = $((N - 1))
cat "$S/gtaxl-unzip" >> "$UB"
tail -c +$N "$UB" | cmp - "$S/gtaxl-unzip"
rm "$S/gtaxl-unzip"

# super: the six images as logical partitions (liblp metadata at the start,
# 1 MiB alignment). Its size must match p19 in gtaxl-gpt.c and gtaxl-layout.c
# in U-Boot. The image is cut after the last logical partition: the free
# space inside super is not written, and liblp keeps no metadata at the end.
HOSTBIN=${HOSTBIN:-$OUT/../../../host/linux-x86/bin}
SUPER_SIZE=$((6963200 * 512))
LP=()
# readonly as in AOSP's super: the recovery maps them writable anyway
# (force_writable in userdebug), addons call blockdev --setrw (§69).
for p in system product system_ext vendor vendor_dlkm system_dlkm; do
	LP+=(--partition "$p:readonly:$(stat -c %s "$OUT/$p.img"):gtaxl" --image "$p=$OUT/$p.img")
done
"$HOSTBIN/lpmake" --metadata-size 65536 --super-name super --metadata-slots 2 \
	--device "super:$SUPER_SIZE" --group "gtaxl:$((SUPER_SIZE - 4 * 1048576))" \
	"${LP[@]}" --output "$S/super-full.img" 2>&1 | grep -v -E 'Invalid sparse file format|will resize' || true
END=$("$HOSTBIN/lpdump" "$S/super-full.img" | awk '/^super: / {e = $4; sub(":", "", e); if (e + 0 > m) m = e + 0} END {print m + 0}')
[ -n "$END" ] && [ "$END" -gt 0 ] || { echo "lpdump: no logical partitions" >&2; exit 1; }
# extents for gtaxl-dm (update-binary remaps after writing super, §69):
# name, first sector, sectors; one extent each, or the map would be wrong
"$HOSTBIN/lpdump" "$S/super-full.img" | awk '/^super: / {
	s = $2; e = $4; sub(":", "", e); n = $5
	if (seen[n]++) { print "lpdump: " n " has more than one extent" > "/dev/stderr"; exit 1 }
	print n, s, e - s }' > "$S/gtaxl/lp-map.txt"
test "$(wc -l < "$S/gtaxl/lp-map.txt")" = 6
head -c $(( (END * 512 + 1048575) / 1048576 * 1048576 )) "$S/super-full.img" > "$S/gtaxl/super.img"
rm "$S/super-full.img"

# boot: the ext4 U-Boot reads, built here with the same features as the one
# TWRP's mke2fs used to make (§65.7): no resize_inode, flex_bg, 64bit,
# metadata_csum; uninit_bg; 4 KiB blocks, 256-byte inodes; 128 MiB (p23, §69).
mkdir -p "$S/bootroot/uboot/android"
cp "$K/Image" "$K/dts/exynos/exynos7870-gtaxlwifi-decon.dtb" "$OUT/ramdisk-all-combined.img" \
	"$DEV/cmdline.txt" "$S/bootroot/uboot/android/"
chmod 755 "$S/bootroot/uboot/android/Image"
chmod 644 "$S/bootroot/uboot/android/"{exynos7870-gtaxlwifi-decon.dtb,ramdisk-all-combined.img,cmdline.txt}
"$HOSTBIN/mke2fs" -F -q -t ext4 -b 4096 -I 256 \
	-O ^resize_inode,^flex_bg,^64bit,^metadata_csum,^metadata_csum_seed,^orphan_file,^fast_commit,uninit_bg \
	-E root_owner=0:0 -L boot -d "$S/bootroot" "$S/gtaxl/boot.img" 32768
"$HOSTBIN/e2fsck" -fn "$S/gtaxl/boot.img" > /dev/null
rm -rf "$S/bootroot"
cp "$UBOOT" "$S/gtaxl/u-boot.img"
# LineageOS's backuptool, as its own packages carry it (install/bin): an
# update keeps what addons listed in /system/addon.d (§69)
mkdir -p "$S/gtaxl/install/bin"
cp "$OUT/install/bin/backuptool.sh" "$OUT/install/bin/backuptool.functions" "$S/gtaxl/install/bin/"

# list for update-binary: sha256, name, offset in MiB, size, size in MiB.
# super at LBA 507904, boot at LBA 8003584 (p23 of the v2 layout, §69)
( cd "$S/gtaxl" && for e in super.img:248 boot.img:3908 u-boot.img:60; do
	f=${e%%:*}; n=$(stat -c %s "$f")
	printf '%s %s %s %s %s\n' "$(sha256sum < "$f" | cut -d' ' -f1)" "$f" "${e##*:}" "$n" $(( (n + 1048575) / 1048576 ))
done ) > "$S/gtaxl/sha256.txt"
test "$(stat -c %s "$S/gtaxl/boot.img")" = $((262144 * 512))
test $(( 248 + $(stat -c %s "$S/gtaxl/super.img") / 1048576 )) -le 3648

# OTA metadata, as in LineageOS's own packages: the Updater app refuses a zip
# without META-INF/com/android/metadata.pb (type, date, SDK and security patch
# level of the build installed), and the recovery reads it for its security
# patch downgrade check (§65.12). The values come from build.prop inside
# system.img, not from $OUT/system/build.prop: any m after the image was made
# rewrites the latter with a new date.
mkdir "$S/sysx"
BP=$S/sysx/build.prop
"$HOSTBIN/debugfs" -R "dump /system/build.prop $BP" "$OUT/system.img" 2> /dev/null
test -s "$BP"
prop() { grep -m1 "^$1=" "$BP" | cut -d= -f2-; }
mkdir -p "$S/META-INF/com/android"
python3 - "$S/META-INF/com/android" gtaxlwifi "$(prop ro.build.fingerprint)" \
	"$(prop ro.build.version.incremental)" "$(prop ro.build.date.utc)" \
	"$(prop ro.build.version.sdk)" "$(prop ro.build.version.security_patch)" << 'PY'
import sys
out, device, fingerprint, incremental, timestamp, sdk, spl = sys.argv[1:]
assert all((fingerprint, incremental, timestamp, sdk, spl)), sys.argv
# protobuf by hand (ota_metadata.proto in build/make/tools/releasetools):
# varints and length-delimited fields are all this message needs
def varint(n):
    b = bytearray()
    while True:
        b.append((n & 0x7f) | (0x80 if n > 0x7f else 0))
        n >>= 7
        if not n:
            return bytes(b)
def num(field, n):
    return varint(field << 3) + varint(n)
def blob(field, data):
    data = data.encode() if isinstance(data, str) else data
    return varint(field << 3 | 2) + varint(len(data)) + data
pre = blob(1, device)                                 # DeviceState.device
post = (blob(1, device) + blob(2, fingerprint) + blob(3, incremental)
        + num(4, int(timestamp)) + blob(5, sdk) + blob(6, spl))
ota = num(1, 2) + blob(5, pre) + blob(6, post)        # type BLOCK, pre, post
open(out + "/metadata.pb", "wb").write(ota)
open(out + "/metadata", "w").write(
    "ota-type=BLOCK\n"
    f"post-build={fingerprint}\npost-build-incremental={incremental}\n"
    f"post-sdk-level={sdk}\npost-security-patch-level={spl}\n"
    f"post-timestamp={timestamp}\npre-device={device}\n")
PY
rm -rf "$S/sysx"

# The images go in 64 MiB pieces, each with its offset in MiB (parts.txt):
# the unzip of LineageOS Recovery (Android's ziptool) extracts an entry to
# memory before writing it to stdout, and a whole super.img is out of memory
# on a 2 GB tablet (§65.12). TWRP's busybox unzip streams; pieces suit both.
PART_MIB=64
mkdir "$S/gtaxl/parts"
( cd "$S/gtaxl" && for e in super.img:248 boot.img:3908 u-boot.img:60; do
	f=${e%%:*}; mib=${e##*:}
	split -b $((PART_MIB * 1048576)) -d -a 3 "$f" "parts/$f."
	for q in parts/"$f".*; do
		printf '%s %s\n' "$q" $(( mib + 10#${q##*.} * PART_MIB ))
	done
	rm "$f"
done ) > "$S/gtaxl/parts.txt"

make_zip
cat "$S/gtaxl/parts.txt" | tail -3
cat "$S/gtaxl/sha256.txt"
