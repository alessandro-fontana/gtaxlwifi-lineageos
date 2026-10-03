#!/bin/bash
# Builds the image for the RECOVERY partition (docs §65.12): LineageOS
# Recovery started by U-Boot.
#
#   tools/migration/build-recovery.sh <output.img>
#
# S-BOOT boots RECOVERY as an Android boot image. Here its "kernel" is U-Boot,
# padded to 2 MiB, followed by a FIT with the compressed kernel, the DTB, the
# recovery ramdisk built by LineageOS (m recoveryimage) and a "cmdline" image
# with bootargs_recovery; there is no "ramdisk". S-BOOT refuses ramdisks over
# 14 MiB and kernels over 0x3d7ff00 bytes (read from its code). U-Boot finds
# the FIT 2 MiB after the address it was loaded at, and boots it
# (gx_recovery_payload in the board code, gx_boot_recovery in the env).
#
# Variables: OUT (default out/target/product/gtaxlwifi), UBOOT_BIN (default
# /work/uboot/src/u-boot.bin), UBOOT_TOOLS (mkimage, mkbootimg, stub-dt.img).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
DEV=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-/work/aosp/lineage/out/target/product/gtaxlwifi}
UBOOT_BIN=${UBOOT_BIN:-/work/uboot/src/u-boot.bin}
UBOOT_TOOLS=${UBOOT_TOOLS:-/work/uboot/tools}
MKIMAGE=${MKIMAGE:-/work/uboot/src/tools/mkimage}
IMG=$(realpath -m "${1:?output image}")
K=$OUT/obj/KERNEL_OBJ/arch/arm64/boot
LIMIT=$((77824 * 512))	# RECOVERY, p10: 38 MiB

S=$(mktemp -d); trap 'rm -rf "$S"' EXIT
gzip -9 -n -c "$K/Image" > "$S/Image.gz"
cp "$K/dts/exynos/exynos7870-gtaxlwifi-decon.dtb" "$S/board.dtb"
cp "$OUT/ramdisk-recovery.img" "$S/ramdisk.img"
cp "$DEV/recovery-cmdline.txt" "$S/cmdline.txt"

# Load addresses as in U-Boot's environment: kernel_addr_r, fdt_addr_r,
# ramdisk_addr_r (informative: imxtract gets the destinations from the env).
# S-BOOT loads the whole image at 0x4007f800, so U-Boot runs at 0x40080000
# and finds the FIT at 0x40280000.
cat > "$S/recovery.its" << 'ITS'
/dts-v1/;
/ {
	description = "gtaxlwifi LineageOS Recovery";
	#address-cells = <1>;
	images {
		kernel {
			data = /incbin/("Image.gz");
			type = "kernel"; arch = "arm64"; os = "linux";
			compression = "gzip";
			load = <0x50000000>; entry = <0x50000000>;
			hash { algo = "sha256"; };
		};
		fdt {
			data = /incbin/("board.dtb");
			type = "flat_dt"; arch = "arm64"; compression = "none";
			load = <0x4a000000>;
			hash { algo = "sha256"; };
		};
		ramdisk {
			data = /incbin/("ramdisk.img");
			type = "ramdisk"; arch = "arm64"; os = "linux";
			compression = "none";
			load = <0x4b000000>;
			hash { algo = "sha256"; };
		};
		cmdline {
			data = /incbin/("cmdline.txt");
			type = "firmware"; compression = "none";
		};
	};
	configurations {
		default = "recovery";
		recovery { kernel = "kernel"; fdt = "fdt"; ramdisk = "ramdisk"; };
	};
};
ITS
# Reproducible: mkimage writes the creation time into the FIT, the only thing
# that changed between two builds of the same inputs. U-Boot does not use it
# to boot; 0 unless SOURCE_DATE_EPOCH says otherwise (gzip -n above for the
# kernel, mkbootimg's id is a hash of the contents).
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
( cd "$S" && "$MKIMAGE" -f recovery.its recovery.fit > /dev/null )
[ -n "${KEEP_FIT:-}" ] && cp "$S/recovery.fit" "$KEEP_FIT"
# U-Boot, zeros up to 2 MiB (GX_RECOVERY_FIT_OFFSET), the FIT
[ "$(stat -c %s "$UBOOT_BIN")" -lt $((2 * 1048576)) ] || { echo "U-Boot over 2 MiB" >&2; exit 1; }
cp "$UBOOT_BIN" "$S/kernel.bin"
truncate -s $((2 * 1048576)) "$S/kernel.bin"
cat "$S/recovery.fit" >> "$S/kernel.bin"
[ "$(stat -c %s "$S/kernel.bin")" -le $((0x3d7ff00)) ] || { echo "kernel field over S-BOOT's limit" >&2; exit 1; }
"$UBOOT_TOOLS/mkbootimg/mkbootimg" --kernel "$S/kernel.bin" \
	--dt "$UBOOT_TOOLS/stub-dt.img" --pagesize 2048 -o "$IMG"
printf 'SEANDROIDENFORCE' >> "$IMG"
n=$(stat -c %s "$IMG")
[ "$n" -le "$LIMIT" ] || { echo "$IMG: $n bytes, RECOVERY holds $LIMIT" >&2; rm -f "$IMG"; exit 1; }
echo "$IMG: $n bytes (RECOVERY: $LIMIT), FIT $(stat -c %s "$S/recovery.fit")"
sha256sum "$IMG"
