#!/bin/bash
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: GPL-2.0-only
#
# Builds loader.bin with the tree's clang and lld, and libfdt from
# external/dtc. Used by bootimg/mkbootimg.mk; runnable by hand:
#
#   bootloader/loader/build.sh <output.bin> [<top of the Android tree>]
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(realpath -m "${1:?output .bin}")
TOP=${2:-${ANDROID_BUILD_TOP:-$(cd "$HERE/../../../../.." && pwd)}}
CLANG_DIR=${CLANG_DIR:-$(ls -d "$TOP"/prebuilts/clang/host/linux-x86/clang-r* | sort | tail -1)}
VERSION=${LOADER_VERSION:-gtaxl-loader-1}
FDT=$TOP/external/dtc/libfdt
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

CFLAGS=(--target=aarch64-none-elf -ffreestanding -fno-builtin -nostdinc
	-isystem "$("$CLANG_DIR/bin/clang" -print-resource-dir)/include"
	-I "$HERE/include" -I "$FDT" -I "$HERE"
	-O2 -Wall -Werror -mgeneral-regs-only -fno-pic -fno-pie -mcmodel=small
	-fno-stack-protector -fno-asynchronous-unwind-tables -ffunction-sections
	-DLOADER_VERSION="\"$VERSION\"")
objs=()
for src in "$HERE"/start.S "$HERE"/loader.c "$HERE"/mmu.c "$HERE"/lz4.c "$HERE"/pmic.c "$HERE"/emmc.c "$HERE"/ext4.c "$HERE"/sha256.c "$HERE"/libc.c \
	"$FDT"/fdt.c "$FDT"/fdt_ro.c "$FDT"/fdt_rw.c "$FDT"/fdt_wip.c; do
	o=$T/$(basename "$src").o
	extra=()
	case $src in "$FDT"/*) extra=(-Wno-error) ;; esac
	"$CLANG_DIR/bin/clang" "${CFLAGS[@]}" "${extra[@]}" -c "$src" -o "$o"
	objs+=("$o")
done
"$CLANG_DIR/bin/ld.lld" -T "$HERE/linker.lds" --gc-sections -static -nostdlib \
	-o "$T/loader.elf" "${objs[@]}"
"$CLANG_DIR/bin/llvm-objcopy" -O binary "$T/loader.elf" "$OUT"
cp "$T/loader.elf" "${OUT%.bin}.elf"
