/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * One register of the PMIC (s2mpu05, 0x66 on HSI2C0 at 0x10510000), polled:
 * the power-on source, which decides the charging mode (docs §42.5, §55.3).
 * The sequence is U-Boot's exynos_hs_i2c driver for the Exynos7 flavour of
 * the block (uboot-hsi2c-exynos7.patch): completion and errors are in
 * INT_STATUS, not TRANS_STATUS. Before it, the four gates of the block in
 * CMU_MIF and the SDA/SCL pads (gpm0-0/1, function 2), which a warm reboot
 * leaves off (§42.11): what U-Boot's board code did.
 */

#include "loader.h"

#define CMU_MIF_GAT_0828	0x10460828UL	/* bit 14: AP_PCLKS */
#define CMU_MIF_GAT_0840	0x10460840UL	/* bits 0, 2, 3: AP_PCLKM, IPCLK, ITCLK */
#define PINCTRL_MIF_GPM0_CON	0x10530000UL

#define HSI2C			0x10510000UL
#define USI_CTL			0x00
#define USI_FIFO_CTL		0x04
#define USI_TRAILING_CTL	0x08
#define USI_INT_EN		0x20
#define USI_INT_STAT		0x24
#define USI_FIFO_STAT		0x30
#define USI_TXDATA		0x34
#define USI_RXDATA		0x38
#define USI_CONF		0x40
#define USI_AUTO_CONF		0x44
#define USI_TIMEOUT		0x48
#define USI_TRANS_STATUS	0x50
#define USI_TIMING_FS1		0x60
#define USI_TIMING_FS2		0x64
#define USI_TIMING_FS3		0x68
#define USI_TIMING_SLA		0x6c
#define I2C_ADDR		0x70

#define CTL_FUNC_MODE_I2C	(1u << 0)
#define CTL_MASTER		(1u << 3)
#define CTL_RXCHON		(1u << 6)
#define CTL_TXCHON		(1u << 7)
#define CTL_SW_RST		(1u << 31)
#define FIFO_EN			(3u << 0)
#define CONF_AUTO_MODE		(1u << 31)
#define AUTO_READ		(1u << 16)
#define AUTO_STOP		(1u << 17)
#define AUTO_MASTER_RUN		(1u << 31)
#define TIMEOUT_EN		(1u << 31)
#define TRANS_MASTER_BUSY	(1u << 17)
#define FIFO_RX_EMPTY		(1u << 24)
#define FIFO_TX_FULL		(1u << 7)
#define INT_TRANS_DONE		(1u << 7)
#define INT_TRANS_ABORT		(1u << 8)
#define INT_NO_DEV_ACK		(1u << 9)
#define INT_NO_DEV		(1u << 10)
#define INT_DONE_MASK		(INT_TRANS_DONE | INT_TRANS_ABORT | INT_NO_DEV_ACK | INT_NO_DEV)

#define CLKIN_HZ		66625000u	/* dout_mif_hsi2c, measured */
#define BUS_HZ			100000u		/* U-Boot's default: no clock-frequency */
#define TIMER_HZ		26000000u	/* arch timer; CNTFRQ is left at 0 */

static inline unsigned int rd(unsigned long a) { return *(volatile unsigned int *)a; }
static inline void wr(unsigned int v, unsigned long a) { *(volatile unsigned int *)a = v; }

static unsigned long ticks(void)
{
	unsigned long v;
	asm volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(v));
	return v;
}

/* wait until (reg & mask) == want, at most 10 ms */
static int wait_bits(unsigned long reg, unsigned int mask, unsigned int want)
{
	unsigned long end = ticks() + TIMER_HZ / 100;

	while ((rd(reg) & mask) != want)
		if (ticks() > end)
			return -1;
	return 0;
}

static int wait_done(void)
{
	unsigned long end = ticks() + TIMER_HZ / 100;
	unsigned int st;

	while (!((st = rd(HSI2C + USI_INT_STAT)) & INT_DONE_MASK))
		if (ticks() > end)
			return -1;
	wr(st, HSI2C + USI_INT_STAT);
	return (st & (INT_TRANS_ABORT | INT_NO_DEV_ACK | INT_NO_DEV)) ? -2 : 0;
}

static void start(unsigned char chip, unsigned int len, int read, int stop)
{
	wr(rd(HSI2C + USI_TIMEOUT) & ~TIMEOUT_EN, HSI2C + USI_TIMEOUT);
	wr((chip & 0x3ffu) << 10, HSI2C + I2C_ADDR);
	wr((read ? CTL_RXCHON : CTL_TXCHON) | CTL_FUNC_MODE_I2C | CTL_MASTER,
	   HSI2C + USI_CTL);
	wr(len | AUTO_MASTER_RUN | (stop ? AUTO_STOP : 0) | (read ? AUTO_READ : 0),
	   HSI2C + USI_AUTO_CONF);
	wr(INT_DONE_MASK, HSI2C + USI_INT_STAT);
}

static void bus_init(void)
{
	unsigned int ftl, cycle = 0, div, u0, i;

	wr(rd(CMU_MIF_GAT_0828) | (1u << 14), CMU_MIF_GAT_0828);
	wr(rd(CMU_MIF_GAT_0840) | (1u << 0) | (1u << 2) | (1u << 3), CMU_MIF_GAT_0840);
	wr((rd(PINCTRL_MIF_GPM0_CON) & ~0xffu) | 0x22, PINCTRL_MIF_GPM0_CON);

	wr(rd(HSI2C + USI_CTL) | CTL_SW_RST, HSI2C + USI_CTL);
	wr(rd(HSI2C + USI_CTL) & ~CTL_SW_RST, HSI2C + USI_CTL);

	/* FPCLK / FI2C = (CLK_DIV + 1) * (TSCLK_L + TSCLK_H + 2) + 8 + 2 * FLT */
	ftl = (rd(HSI2C + USI_CONF) >> 16) & 7;
	u0 = CLKIN_HZ / BUS_HZ - 8 - 2 * ftl;
	for (div = 0, i = 0; i < 256; i++) {
		unsigned int u1 = u0 / (i + 1);
		if (u1 < 512 && u1 > 4) {
			cycle = u1 - 2;
			div = i;
			break;
		}
	}
	wr(0xff, HSI2C + USI_TRAILING_CTL);
	wr(rd(HSI2C + USI_TIMEOUT) & ~TIMEOUT_EN, HSI2C + USI_TIMEOUT);
	wr(rd(HSI2C + USI_CONF) | CONF_AUTO_MODE, HSI2C + USI_CONF);
	wr(INT_DONE_MASK, HSI2C + USI_INT_EN);
	wr(FIFO_EN, HSI2C + USI_FIFO_CTL);
	wr((cycle / 2) << 24 | (cycle / 2) << 16 | (cycle / 2) << 8, HSI2C + USI_TIMING_FS1);
	wr((cycle / 4) << 24 | (cycle / 2) << 8 | (cycle / 2), HSI2C + USI_TIMING_FS2);
	wr(div << 16 | cycle, HSI2C + USI_TIMING_FS3);
	wr(cycle / 4, HSI2C + USI_TIMING_SLA);
}

/* 0 and *val on success; -1 timeout, -2 no ACK / abort */
int pmic_read(unsigned char chip, unsigned char reg, unsigned char *val)
{
	int ret;

	bus_init();

	/* the register address, no stop: a repeated start follows */
	start(chip, 1, 0, 0);
	if (wait_bits(HSI2C + USI_FIFO_STAT, FIFO_TX_FULL, 0))
		return -1;
	wr(reg, HSI2C + USI_TXDATA);
	ret = wait_done();
	wr(CTL_FUNC_MODE_I2C, HSI2C + USI_CTL);
	if (ret)
		goto out;

	start(chip, 1, 1, 1);
	if (wait_bits(HSI2C + USI_FIFO_STAT, FIFO_RX_EMPTY, 0)) {
		ret = -1;
		goto out;
	}
	*val = rd(HSI2C + USI_RXDATA) & 0xff;
	ret = wait_done();
	if (!ret)
		ret = wait_bits(HSI2C + USI_TRANS_STATUS, TRANS_MASTER_BUSY, 0);
out:
	wr(CTL_FUNC_MODE_I2C, HSI2C + USI_CTL);
	return ret;
}
