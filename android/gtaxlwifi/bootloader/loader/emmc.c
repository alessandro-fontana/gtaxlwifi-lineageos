/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Reads sectors of the eMMC (DW-MMC at 0x13540000), as U-Boot's dw_mmc does
 * in FIFO mode: S-BOOT has just read the boot image through it, so the card
 * is selected and in transfer state, clocks and bus width set. Nothing here
 * writes to the card. Partitions are found by name in the GPT, as U-Boot did.
 */

#include "loader.h"

#define MMC		0x13540000UL
#define CTRL		0x000
#define BLKSIZ		0x01c
#define BYTCNT		0x020
#define CMDARG		0x028
#define CMD		0x02c
#define RINTSTS		0x044
#define STATUS		0x048
#define BMOD		0x080

#define CTRL_FIFO_RESET	(1u << 1)
#define CTRL_DMA_EN	(1u << 5)
#define CTRL_IDMAC_EN	(1u << 25)
#define BMOD_IDMAC_EN	(1u << 7)

#define INT_RE		(1u << 1)
#define INT_CDONE	(1u << 2)
#define INT_DTO		(1u << 3)
#define INT_RXDR	(1u << 5)
#define INT_RCRC	(1u << 6)
#define INT_DCRC	(1u << 7)
#define INT_RTO		(1u << 8)
#define INT_DRTO	(1u << 9)
#define INT_HTO		(1u << 10)
#define INT_SBE		(1u << 13)
#define INT_EBE		(1u << 15)
#define INT_DATA_ERR	(INT_DCRC | INT_DRTO | INT_HTO | INT_SBE | INT_EBE)

#define CMD_RESP_EXP	(1u << 6)
#define CMD_CHECK_CRC	(1u << 8)
#define CMD_DATA_EXP	(1u << 9)
#define CMD_SEND_STOP	(1u << 12)
#define CMD_PRV_DAT_WAIT (1u << 13)
#define CMD_USE_HOLD	(1u << 29)
#define CMD_START	(1u << 31)

#define STATUS_BUSY	(1u << 9)
#define HCON		0x070	/* bit 27: 64-bit IDMAC descriptors */
#define DBADDRL		0x088
#define DBADDRU		0x08c
#define IDSTS32		0x08c
#define IDSTS64		0x090
#define BMOD_FB		(1u << 1)
#define IDSTS_TI	(1u << 0)
#define IDSTS_RI	(1u << 1)
#define IDMAC_LD	(1u << 2)
#define IDMAC_FS	(1u << 3)
#define IDMAC_CH	(1u << 4)
#define IDMAC_OWN	(1u << 31)
#define TIMER_HZ	26000000u

static inline unsigned int rd(unsigned long o) { return *(volatile unsigned int *)(MMC + o); }
static inline void wr(unsigned int v, unsigned long o) { *(volatile unsigned int *)(MMC + o) = v; }

static unsigned long ticks(void)
{
	unsigned long v;
	asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(v));
	return v;
}

/* the first failure, with the state of the controller */
static int fail(const char *where)
{
	static int logged;

	if (!logged++) {
		loader_log(where, 1);
		loader_log("ctrl", rd(CTRL));
		loader_log("status", rd(STATUS));
		loader_log("rint", rd(RINTSTS));
		loader_log("cmd", rd(CMD));
		loader_log("bytcnt", rd(BYTCNT));
		loader_log("bmod", rd(BMOD));
		loader_log("clkena", rd(0x010));
		loader_log("ctype", rd(0x018));
		loader_log("verid", rd(0x06c));
	}
	return -1;
}

/* clean and invalidate [p, p + len) by VA: the IDMAC does not snoop */
static void dcache_civac(const void *p, unsigned long len)
{
	unsigned long a = (unsigned long)p & ~63UL, e = (unsigned long)p + len;

	for (; a < e; a += 64)
		asm volatile("dc civac, %0" : : "r"(a) : "memory");
	asm volatile("dsb sy" : : : "memory");
}

/*
 * One descriptor, 64-bit form (HCON bit 27, as on this SoC) or 32-bit, and
 * a 4 KiB bounce buffer: every read here is at most one ext4 block. The
 * IDMAC is how U-Boot and Linux use this controller; its FIFO cannot be
 * read by the CPU here (64 entries stayed in it whatever the access width,
 * §73), Samsung's FMP being the likely reason.
 */
static uint32_t desc[8] __attribute__((aligned(64)));
static unsigned char bounce[4096] __attribute__((aligned(64)));

/* n sectors (at most 8) from lba into buf; 0 or -1 */
int emmc_read(unsigned long lba, unsigned int n, void *buf)
{
	unsigned long end = ticks() + TIMER_HZ / 4;	/* 250 ms */
	unsigned long d = (unsigned long)desc, b = (unsigned long)bounce;
	int dma64 = (rd(HCON) >> 27) & 1;
	unsigned int mask;

	if (!n || n > 8)
		return -1;
	while (rd(STATUS) & STATUS_BUSY)
		if (ticks() > end)
			return fail("busy");

	wr(0xffffffff, RINTSTS);
	wr(rd(CTRL) | CTRL_FIFO_RESET, CTRL);
	while (rd(CTRL) & CTRL_FIFO_RESET)
		if (ticks() > end)
			return fail("fiforst");

	memset(desc, 0, sizeof(desc));
	if (dma64) {
		desc[0] = IDMAC_OWN | IDMAC_CH | IDMAC_FS | IDMAC_LD;
		desc[2] = n * 512;
		desc[4] = b;
		desc[5] = b >> 32;
		desc[6] = d + 32;
		desc[7] = (d + 32) >> 32;
		wr(0xffffffff, IDSTS64);
		wr(d, DBADDRL);
		wr(d >> 32, DBADDRU);
	} else {
		desc[0] = IDMAC_OWN | IDMAC_CH | IDMAC_FS | IDMAC_LD;
		desc[1] = n * 512;
		desc[2] = b;
		desc[3] = d + 16;
		wr(0xffffffff, IDSTS32);
		wr(d, DBADDRL);
	}
	dcache_civac(desc, sizeof(desc));
	dcache_civac(bounce, n * 512);

	wr(rd(CTRL) | CTRL_IDMAC_EN | CTRL_DMA_EN, CTRL);
	wr(rd(BMOD) | BMOD_FB | BMOD_IDMAC_EN, BMOD);
	wr(512, BLKSIZ);
	wr(n * 512, BYTCNT);

	wr(lba, CMDARG);	/* high capacity: sector address */
	wr((n > 1 ? 18 : 17) | CMD_START | CMD_USE_HOLD | CMD_PRV_DAT_WAIT |
	   CMD_RESP_EXP | CMD_CHECK_CRC | CMD_DATA_EXP | (n > 1 ? CMD_SEND_STOP : 0),
	   CMD);
	while (!((mask = rd(RINTSTS)) & INT_CDONE))
		if (ticks() > end)
			return fail("cdone");
	if (mask & (INT_RTO | INT_RE | INT_RCRC))
		return fail("resp");

	while (!((mask = rd(RINTSTS)) & INT_DTO)) {
		if (mask & INT_DATA_ERR)
			return fail("data");
		if (ticks() > end)
			return fail("dto");
	}
	/*
	 * The IDMAC hands the descriptor back by clearing OWN. IDSTS stays at
	 * zero on this controller with its interrupts masked (measured: OWN
	 * cleared, IDSTS 0 in both forms, §73), so it is not waited for.
	 */
	for (;;) {
		dcache_civac(desc, sizeof(desc));
		if (!(*(volatile uint32_t *)desc & IDMAC_OWN))
			break;
		if (ticks() > end)
			return fail("own");
	}
	wr(IDSTS_RI | IDSTS_TI, dma64 ? IDSTS64 : IDSTS32);
	wr(0xffffffff, RINTSTS);
	wr(rd(CTRL) & ~CTRL_DMA_EN, CTRL);

	dcache_civac(bounce, n * 512);
	memcpy(buf, bounce, n * 512);
	return 0;
}

/*
 * First LBA and length of the GPT entry called name (ASCII, compared with
 * the UTF-16LE of the entry). Standard GPT at LBA 1, 128-byte entries.
 * Returns 0, -1 if the table cannot be read, -2 if there is no such entry.
 */
int gpt_find(const char *name, unsigned long *first, unsigned long *count)
{
	static unsigned int sec[128] __attribute__((aligned(8)));
	unsigned long entries;
	unsigned int num, esize, i, k;
	const unsigned char *b = (const unsigned char *)sec;

	if (emmc_read(1, 1, sec) || memcmp(b, "EFI PART", 8))
		return -1;
	entries = *(const unsigned long *)(b + 72);
	num = *(const unsigned int *)(b + 80);
	esize = *(const unsigned int *)(b + 84);
	if (esize != 128 || num > 256)
		return -1;
	for (i = 0; i < num; i++) {
		const unsigned char *e;
		size_t l = strlen(name);

		if (i % 4 == 0 && emmc_read(entries + i / 4, 1, sec))
			return -1;
		e = b + (i % 4) * 128;
		for (k = 0; k < 36; k++) {
			unsigned int c = e[56 + 2 * k] | e[57 + 2 * k] << 8;
			if (c != (k < l ? (unsigned char)name[k] : 0))
				break;
		}
		if (k == 36) {
			unsigned long lo = *(const unsigned long *)(e + 32);
			unsigned long hi = *(const unsigned long *)(e + 40);
			*first = lo;
			*count = hi - lo + 1;
			return 0;
		}
	}
	return -2;
}
