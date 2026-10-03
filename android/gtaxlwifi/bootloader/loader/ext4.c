/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Just enough ext4 to read a small file from Samsung's EFS, read-only: the
 * superblock, one group descriptor, inodes with an extent tree of depth 0
 * (or the old block map), linear directories. The EFS of this tablet is a
 * 20 MiB ext4 with 4 KiB blocks and files of one block; anything outside
 * what is handled here is an error, never a guess.
 */

#include "loader.h"

#define EXT4_EXTENTS_FL		0x80000
#define EXT4_EXT_MAGIC		0xf30a
#define INCOMPAT_64BIT		0x80

static unsigned char blk[4096] __attribute__((aligned(8)));

static unsigned int le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned int le32(const unsigned char *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24;
}

struct fs {
	unsigned long part;		/* first sector of the partition */
	unsigned int bsize, ipg, isize, dsize, first;
};

/* one filesystem block into blk */
static int read_block(const struct fs *fs, unsigned long b)
{
	return emmc_read(fs->part + b * (fs->bsize / 512), fs->bsize / 512, blk);
}

/* the 60 bytes of i_block and the size of inode ino */
static int read_inode(const struct fs *fs, unsigned int ino, unsigned char *iblock,
		      unsigned int *size, unsigned int *flags)
{
	unsigned int group = (ino - 1) / fs->ipg, index = (ino - 1) % fs->ipg;
	unsigned long gd_block = fs->first + 1, table, off;

	off = (unsigned long)group * fs->dsize;
	if (read_block(fs, gd_block + off / fs->bsize))
		return -1;
	table = le32(blk + off % fs->bsize + 8);	/* bg_inode_table_lo */
	off = (unsigned long)index * fs->isize;
	if (read_block(fs, table + off / fs->bsize))
		return -1;
	*size = le32(blk + off % fs->bsize + 4);
	*flags = le32(blk + off % fs->bsize + 32);
	memcpy(iblock, blk + off % fs->bsize + 40, 60);
	return 0;
}

/* physical block of logical block 0..n of a file */
static long map_block(const unsigned char *iblock, unsigned int flags, unsigned int lblock)
{
	unsigned int i, entries;

	if (!(flags & EXT4_EXTENTS_FL))
		return lblock < 12 ? (long)le32(iblock + 4 * lblock) : -1;
	if (le16(iblock) != EXT4_EXT_MAGIC || le16(iblock + 6) != 0)	/* depth 0 only */
		return -1;
	entries = le16(iblock + 2);
	for (i = 0; i < entries && i < 4; i++) {
		const unsigned char *e = iblock + 12 + 12 * i;
		unsigned int first = le32(e), len = le16(e + 4) & 0x7fff;

		if (lblock >= first && lblock < first + len)
			return ((long)le16(e + 6) << 32 | le32(e + 8)) + (lblock - first);
	}
	return -1;
}

/* inode of name inside directory dir, or 0 */
static unsigned int lookup(const struct fs *fs, unsigned int dir, const char *name, size_t nlen)
{
	unsigned char iblock[60];
	unsigned int size, flags, lb;

	if (read_inode(fs, dir, iblock, &size, &flags))
		return 0;
	for (lb = 0; lb * fs->bsize < size; lb++) {
		long pb = map_block(iblock, flags, lb);
		unsigned int off = 0;

		if (pb <= 0 || read_block(fs, pb))
			return 0;
		while (off + 8 <= fs->bsize) {
			const unsigned char *d = blk + off;
			unsigned int ino = le32(d), rec = le16(d + 4), nl = d[6];

			if (rec < 8 || off + rec > fs->bsize)
				return 0;
			if (ino && nl == nlen && !memcmp(d + 8, name, nlen))
				return ino;
			off += rec;
		}
	}
	return 0;
}

/*
 * Reads up to len bytes of the file at path (absolute) in the ext4 at
 * sector part; returns the bytes read or -1.
 */
long ext4_read_file(unsigned long part, const char *path, void *buf, unsigned int len)
{
	struct fs fs = { .part = part };
	unsigned char iblock[60];
	unsigned int ino = 2, size, flags, done = 0, lb;

	if (emmc_read(part + 2, 2, blk) || le16(blk + 56) != 0xef53)
		return -1;
	fs.bsize = 1024u << le32(blk + 24);
	fs.first = le32(blk + 20);
	fs.ipg = le32(blk + 40);
	fs.isize = le16(blk + 88);
	fs.dsize = (le32(blk + 96) & INCOMPAT_64BIT) ? le16(blk + 254) : 32;
	if (fs.bsize > sizeof(blk) || !fs.ipg || !fs.isize || !fs.dsize)
		return -1;

	while (*path) {
		const char *end;

		while (*path == '/')
			path++;
		end = strchr(path, '/');
		if (!end)
			end = path + strlen(path);
		if (end == path)
			break;
		ino = lookup(&fs, ino, path, end - path);
		if (!ino)
			return -1;
		path = end;
	}

	if (read_inode(&fs, ino, iblock, &size, &flags))
		return -1;
	if (len > size)
		len = size;
	for (lb = 0; done < len; lb++) {
		long pb = map_block(iblock, flags, lb);
		unsigned int n = len - done;

		if (pb <= 0 || read_block(&fs, pb))
			return -1;
		if (n > fs.bsize)
			n = fs.bsize;
		memcpy((unsigned char *)buf + done, blk, n);
		done += n;
	}
	return done;
}
