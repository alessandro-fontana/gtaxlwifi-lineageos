#!/bin/bash
# Runs U-Boot's partition table migration (gtaxl-layout.c) on the PC, on a
# sparse disk image, and checks it against gtaxl-gpt (docs §69, §71):
#   1. factory table + the images of an install zip -> the v2 table of
#      gtaxl-gpt, primary and backup, with metadata, misc and USERDATA
#      heads zeroed; a second run writes nothing;
#   2. gtaxl-gpt revert of that table -> the factory table, and U-Boot then
#      writes nothing (no super image at the start of SYSTEM);
#   3. a stock tablet (factory table, no images): nothing written.
#
#   tools/migration/test-layout.sh <factory-gpt-lba1-33.bin> <install.zip>
#
# UB (default /work/uboot/src) is the U-Boot tree. Needs gcc and zlib.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
FAC=$(realpath "${1:?factory GPT, LBA 1-33}")
ZIP=$(realpath "${2:?install zip}")
UB=${UB:-/work/uboot/src}
SECTORS=30777344
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
cd "$W"

cat > harness.c <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
struct blk_desc { FILE *f; u64 lba; };
struct cmd_tbl { int unused; };
#define ARCH_DMA_MINALIGN 64
#define CMD_RET_SUCCESS 0
#define CMD_RET_FAILURE 1
static struct blk_desc gdev;
static struct blk_desc *blk_get_dev(const char *i, int n) { return &gdev; }
static unsigned long blk_dread(struct blk_desc *b, u64 lba, u64 n, void *buf)
{ fseeko(b->f, lba * 512, SEEK_SET); return fread(buf, 512, n, b->f); }
static unsigned long blk_dwrite(struct blk_desc *b, u64 lba, u64 n, const void *buf)
{ fseeko(b->f, lba * 512, SEEK_SET); return fwrite(buf, 512, n, b->f); }
static void *memalign(size_t a, size_t s) { return malloc(s); }
#define crc32(seed, p, n) ((u32)crc32((seed), (p), (n)))
#define U_BOOT_CMD(...)
C
sed '/^#include/d' "$UB/board/samsung/exynos-mobile/gtaxl-layout.c" >> harness.c
cat >> harness.c <<'C'
int main(int argc, char **argv)
{
	gdev.f = fopen(argv[1], "r+b");
	gdev.lba = strtoull(argv[2], NULL, 10);
	return do_gtaxl_layout(NULL, 0, 0, NULL);
}
C
gcc -O1 -w -o harness harness.c -lz
gcc -O2 -Wall -o gtaxl-gpt "$HERE/../../ota/gtaxl-gpt.c"

gpt() { dd if="$1" bs=512 skip="$2" count=33 status=none; }
fail() { echo "FAIL: $*"; exit 1; }

# 1. factory + images
truncate -s $((SECTORS * 512)) disk.img
dd if="$FAC" of=disk.img bs=512 seek=1 conv=notrunc status=none
unzip -q -o "$ZIP" 'gtaxl/parts/super.img.000' 'gtaxl/parts/boot.img.*'
dd if=gtaxl/parts/super.img.000 of=disk.img bs=1048576 seek=248 conv=notrunc status=none
cat gtaxl/parts/boot.img.* | dd of=disk.img bs=1048576 seek=3908 conv=notrunc status=none
./harness disk.img $SECTORS || fail "migration from factory"
./gtaxl-gpt migrate "$FAC" $SECTORS v2.bin v2-b.bin
cmp -s <(gpt disk.img 1) v2.bin || fail "primary GPT differs from gtaxl-gpt"
cmp -s <(gpt disk.img $((SECTORS - 33))) v2-b.bin || fail "backup GPT differs from gtaxl-gpt"
for lba in 8265728 8298496 8300544; do
	[ "$(dd if=disk.img bs=512 skip=$lba count=2048 status=none | tr -d '\000' | wc -c)" = 0 ] \
		|| fail "head at LBA $lba not zeroed"
done
before=$(gpt disk.img 1 | md5sum)
./harness disk.img $SECTORS || fail "second boot"
[ "$before" = "$(gpt disk.img 1 | md5sum)" ] || fail "second boot rewrote the table"
echo "ok: factory -> v2, same table as gtaxl-gpt, heads zeroed, idempotent"

# 2. revert
./gtaxl-gpt revert v2.bin $SECTORS rev.bin rev-b.bin
cmp -s rev.bin "$FAC" || fail "revert is not the factory table"
dd if=rev.bin of=disk.img bs=512 seek=1 conv=notrunc status=none
dd if=rev-b.bin of=disk.img bs=512 seek=$((SECTORS - 33)) conv=notrunc status=none
dd if=/dev/zero of=disk.img bs=512 seek=507904 count=2048 conv=notrunc status=none
if ./harness disk.img $SECTORS; then fail "U-Boot migrated a reverted disk"; fi
cmp -s <(gpt disk.img 1) "$FAC" || fail "U-Boot wrote after the revert"
echo "ok: v2 -> factory, U-Boot then leaves it alone"

# 3. stock
rm disk.img; truncate -s $((SECTORS * 512)) disk.img
dd if="$FAC" of=disk.img bs=512 seek=1 conv=notrunc status=none
if ./harness disk.img $SECTORS; then fail "U-Boot migrated a stock disk"; fi
cmp -s <(gpt disk.img 1) "$FAC" || fail "U-Boot wrote on a stock disk"
echo "ok: stock table untouched"
