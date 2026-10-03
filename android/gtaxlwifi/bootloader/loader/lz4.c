/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * LZ4 legacy frame decoder ("lz4 -l", the format the Linux kernel's unlz4
 * reads): a 32-bit magic, then blocks, each a 32-bit compressed size and an
 * LZ4 block that inflates to at most 8 MiB. Bounds-checked on both sides.
 */

#include "loader.h"

#define LZ4_LEGACY_MAGIC	0x184c2102u
#define LZ4_LEGACY_BLOCK	(8u << 20)

static unsigned int le32(const unsigned char *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

/* one LZ4 block; returns the bytes written or -1 */
static long lz4_block(const unsigned char *src, unsigned long srclen,
		      unsigned char *dst, unsigned long dstlen)
{
	const unsigned char *ip = src, *iend = src + srclen;
	unsigned char *op = dst, *oend = dst + dstlen;

	while (ip < iend) {
		unsigned int token = *ip++;
		unsigned long len = token >> 4;
		unsigned long off;
		const unsigned char *match;

		if (len == 15) {
			unsigned int b;
			do {
				if (ip >= iend)
					return -1;
				b = *ip++;
				len += b;
			} while (b == 255);
		}
		if (len > (unsigned long)(iend - ip) || len > (unsigned long)(oend - op))
			return -1;
		memcpy(op, ip, len);
		op += len;
		ip += len;
		if (ip == iend)		/* the last sequence has no match */
			break;

		if (iend - ip < 2)
			return -1;
		off = ip[0] | ip[1] << 8;
		ip += 2;
		if (off == 0 || off > (unsigned long)(op - dst))
			return -1;
		match = op - off;

		len = (token & 15) + 4;
		if ((token & 15) == 15) {
			unsigned int b;
			do {
				if (ip >= iend)
					return -1;
				b = *ip++;
				len += b;
			} while (b == 255);
		}
		if (len > (unsigned long)(oend - op))
			return -1;
		if (off >= len) {
			memcpy(op, match, len);
			op += len;
		} else {
			while (len--)	/* overlapping copy: byte by byte */
				*op++ = *match++;
		}
	}
	return op - dst;
}

long lz4_legacy_decompress(const void *src, unsigned long srclen,
			   void *dst, unsigned long dstlen)
{
	const unsigned char *ip = src, *iend = ip + srclen;
	unsigned char *op = dst;
	unsigned long left = dstlen;

	if (srclen < 4 || le32(ip) != LZ4_LEGACY_MAGIC)
		return -1;
	ip += 4;
	while (iend - ip >= 4) {
		unsigned int bsize = le32(ip);
		long n;

		if (bsize == LZ4_LEGACY_MAGIC)	/* concatenated frame */
			break;
		ip += 4;
		if (bsize > (unsigned long)(iend - ip))
			return -1;
		n = lz4_block(ip, bsize, op, left < LZ4_LEGACY_BLOCK ? left : LZ4_LEGACY_BLOCK);
		if (n < 0)
			return -1;
		ip += bsize;
		op += n;
		left -= n;
	}
	return op - (unsigned char *)dst;
}
