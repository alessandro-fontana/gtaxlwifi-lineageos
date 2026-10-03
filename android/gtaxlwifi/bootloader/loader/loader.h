/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef LOADER_H
#define LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * The payload follows the loader at a fixed offset inside the "kernel" of
 * the boot image (bootimg/gtaxl_bootimg.py writes it): a header, then the
 * pieces it points at, offsets from the header.
 */
#define PAYLOAD_OFFSET	0x100000	/* 1 MiB after _start */
#define PAYLOAD_MAGIC	"GXPAYLD1"

#define PAYLOAD_RECOVERY	(1u << 0)	/* in RECOVERY: never fall back to it */

struct payload {
	char magic[8];
	uint32_t flags;
	uint32_t kernel_off, kernel_size, kernel_raw_size;	/* lz4 -l */
	uint32_t dtb_off, dtb_size;
	uint32_t ramdisk_off, ramdisk_size;
	uint32_t cmdline_off, cmdline_size;			/* text */
};

unsigned int current_el(void);
int mmu_enable(void);
int pmic_read(unsigned char chip, unsigned char reg, unsigned char *val);
int emmc_read(unsigned long lba, unsigned int n, void *buf);
int gpt_find(const char *name, unsigned long *first, unsigned long *count);
long ext4_read_file(unsigned long part, const char *path, void *buf, unsigned int len);
void sha256(const void *data, size_t len, unsigned char out[32]);
/* a line of "tag=hex" pairs, left in /chosen/gtaxl,loader-log for Android */
void loader_log(const char *tag, unsigned long v);
void mmu_disable(void);
long lz4_legacy_decompress(const void *src, unsigned long srclen,
			   void *dst, unsigned long dstlen);
void jump_to_kernel(unsigned long entry, unsigned long dtb) __attribute__((noreturn));

#endif
