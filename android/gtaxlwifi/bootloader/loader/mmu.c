/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * MMU and caches at EL1, only to make the decompression fast: with the MMU
 * off every data access is Device-nGnRnE, and inflating 40 MB that way
 * takes tens of seconds. An identity map in 1 GiB blocks: the first and the
 * last GiB are devices, the two in between (0x40000000-0xbfffffff, the RAM
 * of this tablet) normal write-back memory. Everything is cleaned and turned
 * off again before the kernel starts, as its boot protocol wants.
 */

#include "loader.h"

#define SCTLR_M		(1UL << 0)
#define SCTLR_C		(1UL << 2)
#define SCTLR_I		(1UL << 12)

#define MAIR_DEVICE	0x00UL	/* attr 0: Device-nGnRnE */
#define MAIR_NORMAL	0xffUL	/* attr 1: Normal, write-back RW-allocate */

/* level 1 block: AttrIndx, AF, SH inner shareable for normal memory */
#define BLOCK		0x1UL
#define AF		(1UL << 10)
#define SH_INNER	(3UL << 8)
#define ATTR(i)		((unsigned long)(i) << 2)
#define XN		((1UL << 53) | (1UL << 54))

/* T0SZ 32: 4 GiB of VA, lookup starts at level 1 (four 1 GiB entries) */
static unsigned long l1_table[512] __attribute__((aligned(4096)));

static inline unsigned long read_sctlr(void)
{
	unsigned long v;
	asm volatile("mrs %0, sctlr_el1" : "=r"(v));
	return v;
}

static inline void write_sctlr(unsigned long v)
{
	asm volatile("msr sctlr_el1, %0\n\tisb" : : "r"(v) : "memory");
}

unsigned int current_el(void)
{
	unsigned long v;
	asm volatile("mrs %0, CurrentEL" : "=r"(v));
	return (v >> 2) & 3;
}

/*
 * Clean and invalidate every data cache level by set/way, as in the
 * Armv8-A ARM's example (D7.2.x): CLIDR gives the levels, CCSIDR the
 * geometry of each.
 */
static void dcache_clean_inval_all(void)
{
	unsigned long clidr, ccsidr, loc, level;

	asm volatile("mrs %0, clidr_el1" : "=r"(clidr));
	loc = (clidr >> 24) & 7;
	for (level = 0; level < loc; level++) {
		unsigned long ctype = (clidr >> (level * 3)) & 7;
		unsigned long line, ways, sets, way_shift, way, set;

		if (ctype < 2)		/* no cache, or instruction only */
			continue;
		asm volatile("msr csselr_el1, %0\n\tisb" : : "r"(level << 1));
		asm volatile("mrs %0, ccsidr_el1" : "=r"(ccsidr));
		line = (ccsidr & 7) + 4;
		ways = ((ccsidr >> 3) & 0x3ff) + 1;
		sets = ((ccsidr >> 13) & 0x7fff) + 1;
		way_shift = __builtin_clz((unsigned int)(ways - 1));
		if (ways == 1)
			way_shift = 0;
		for (way = 0; way < ways; way++)
			for (set = 0; set < sets; set++) {
				unsigned long sw = (way << way_shift) |
					(set << line) | (level << 1);
				asm volatile("dc cisw, %0" : : "r"(sw) : "memory");
			}
	}
	asm volatile("dsb sy\n\tisb" : : : "memory");
}

int mmu_enable(void)
{
	unsigned long tcr, i;

	if (current_el() != 1)
		return -1;		/* stay slow but correct */
	if (read_sctlr() & SCTLR_M)
		mmu_disable();		/* S-BOOT left it on: start clean */

	for (i = 0; i < 4; i++) {
		unsigned long pa = i << 30;
		if (i == 1 || i == 2)
			l1_table[i] = pa | BLOCK | AF | SH_INNER | ATTR(1);
		else
			l1_table[i] = pa | BLOCK | AF | ATTR(0) | XN;
	}

	/* the table was written with the MMU off: make sure memory has it */
	dcache_clean_inval_all();

	/*
	 * T0SZ = 32, IRGN0/ORGN0 = write-back write-allocate, SH0 = inner,
	 * TG0 = 4 KiB, EPD1 = no TTBR1 walks, IPS = 32-bit.
	 */
	tcr = 32UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (0UL << 14) |
	      (1UL << 23) | (0UL << 32);
	asm volatile(
		"msr mair_el1, %0\n\t"
		"msr tcr_el1, %1\n\t"
		"msr ttbr0_el1, %2\n\t"
		"isb\n\t"
		"tlbi vmalle1\n\t"
		"dsb sy\n\t"
		"isb"
		: : "r"(MAIR_DEVICE | (MAIR_NORMAL << 8)), "r"(tcr),
		    "r"((unsigned long)l1_table) : "memory");
	write_sctlr(read_sctlr() | SCTLR_M | SCTLR_C | SCTLR_I);
	return 0;
}

void mmu_disable(void)
{
	unsigned long v = read_sctlr();

	if (!(v & (SCTLR_M | SCTLR_C)))
		return;
	/* write back what the decompression left in the caches, then off */
	dcache_clean_inval_all();
	write_sctlr(v & ~(SCTLR_M | SCTLR_C));
	dcache_clean_inval_all();
	asm volatile("tlbi vmalle1\n\tdsb sy\n\tisb" : : : "memory");
}
