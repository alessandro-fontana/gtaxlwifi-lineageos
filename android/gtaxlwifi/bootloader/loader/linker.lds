/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * S-BOOT loads the boot image's "kernel" at 0x40080000. The payload follows
 * at PAYLOAD_OFFSET (loader.h): code, data, BSS and stack must end before it.
 */
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
	. = 0x40080000;
	_start_image = .;
	.text : { *(.text.start) *(.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) }
	. = ALIGN(16);
	__bss_start = .;
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	__bss_end = .;
	. = . + 0x10000;
	__stack_end = .;
	_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(_end <= _start_image + 0x100000, "loader too big for PAYLOAD_OFFSET")
