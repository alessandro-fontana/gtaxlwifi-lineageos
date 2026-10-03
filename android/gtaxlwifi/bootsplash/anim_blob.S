/* SPDX-License-Identifier: Apache-2.0
 *
 * Links `anim.rle` into the binary. The file is produced by
 * tools/make-bootsplash-anim.py from the build's bootanimation; here it
 * goes in as is, without going through a 258 KB C array that the compiler
 * would have to chew through line by line.
 *
 * WARNING: Soong does not know that this file depends on anim.rle. If the
 * animation is regenerated, this .S must be touched too (or `m installclean`
 * run), otherwise the binary stays the old one.
 */

	.section .rodata
	.balign 8
	.globl _binary_anim_rle_start
	.type _binary_anim_rle_start, @object
_binary_anim_rle_start:
	.incbin "anim.rle"
	.globl _binary_anim_rle_end
	.type _binary_anim_rle_end, @object
_binary_anim_rle_end:
	.byte 0
	.size _binary_anim_rle_start, _binary_anim_rle_end - _binary_anim_rle_start
