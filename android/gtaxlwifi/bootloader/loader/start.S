/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Entry of the gtaxlwifi loader. S-BOOT loads the "kernel" of the boot image
 * at 0x40080000 and jumps there at EL1, with the address of its own device
 * tree in x0 (the same contract U-Boot used, SAVE_PREV_BL_FDT_ADDR).
 */

	.section .text.start, "ax"
	.global _start
_start:
	mov	x19, x0			/* S-BOOT's device tree */
	msr	daifset, #0xf		/* no interrupts, ever */

	adrp	x0, __stack_end
	add	x0, x0, :lo12:__stack_end
	mov	sp, x0

	adrp	x0, __bss_start
	add	x0, x0, :lo12:__bss_start
	adrp	x1, __bss_end
	add	x1, x1, :lo12:__bss_end
1:	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:
	mov	x0, x19
	bl	loader_main
3:	wfi				/* loader_main does not return */
	b	3b

/*
 * jump_to_kernel(entry, dtb): the arm64 boot protocol. MMU and D-cache are
 * already off (mmu_disable), the I-cache is invalidated here.
 */
	.global jump_to_kernel
jump_to_kernel:
	mov	x4, x0
	mov	x0, x1
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	ic	iallu
	dsb	sy
	isb
	br	x4
