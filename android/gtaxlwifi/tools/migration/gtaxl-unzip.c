/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * gtaxl-unzip: writes one entry of a zip to stdout (docs §65.12).
 *
 *   gtaxl-unzip <zip>               <entry>
 *   gtaxl-unzip @<block.map>        <entry>
 *
 * The second form is how the recovery hands over a package installed by the
 * Updater app: uncrypt writes the zip in plain text into its blocks on
 * USERDATA and lists them in a block map (device; size and block size; number
 * of ranges; "start end" block ranges, end excluded). The recovery's own
 * updater reads it like that; unzip cannot. Android's unzip also holds a whole
 * entry in memory for -p, which is why the images are in 64 MiB pieces.
 *
 * Stored and deflated entries, no zip64 (the package is under 4 GiB), CRC-32
 * checked at the end: exit 1 on any error, and the caller discards the output.
 * Static, zlib compiled in (external/zlib), for TWRP and LineageOS Recovery.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

static int fd = -1;
static uint64_t size;           /* bytes of the zip */
static uint64_t bsize = 1;      /* block size of the map, 1 for a plain file */
static uint64_t nranges;
static uint64_t (*ranges)[2];   /* block ranges, [start, end) */

static void die(const char *m)
{
	fprintf(stderr, "gtaxl-unzip: %s%s%s\n", m, errno ? ": " : "", errno ? strerror(errno) : "");
	exit(1);
}

/* read len bytes at offset off of the zip, through the block map if any */
static void read_at(uint64_t off, void *buf, size_t len)
{
	uint8_t *p = buf;

	if (off > size || len > size - off)
		die("read past the end of the zip");
	while (len) {
		uint64_t dev_off = off, chunk = len;

		if (ranges) {
			uint64_t blk = off / bsize, base = 0, i;

			for (i = 0; i < nranges; i++) {
				uint64_t n = ranges[i][1] - ranges[i][0];

				if (blk < base + n)
					break;
				base += n;
			}
			if (i == nranges)
				die("offset outside the block map");
			dev_off = (ranges[i][0] + blk - base) * bsize + off % bsize;
			/* up to the end of this range */
			uint64_t left = (ranges[i][1] - ranges[i][0] - (blk - base)) * bsize - off % bsize;
			if (chunk > left)
				chunk = left;
		}
		ssize_t r = pread(fd, p, chunk, (off_t)dev_off);
		if (r <= 0)
			die("read");
		p += r; off += r; len -= r;
	}
}

static void open_source(const char *path)
{
	if (path[0] != '@') {
		fd = open(path, O_RDONLY);
		if (fd < 0)
			die(path);
		off_t end = lseek(fd, 0, SEEK_END);
		if (end < 0)
			die("lseek");
		size = end;
		return;
	}
	FILE *m = fopen(path + 1, "r");
	char dev[256];
	unsigned long long a, b;

	if (!m)
		die(path + 1);
	if (fscanf(m, "%255s %llu %llu %llu", dev, &a, &b, (unsigned long long *)&nranges) != 4 ||
	    !b || !nranges || nranges > 1000000)
		die("bad block map header");
	size = a; bsize = b;
	ranges = calloc(nranges, sizeof(*ranges));
	if (!ranges)
		die("calloc");
	for (uint64_t i = 0; i < nranges; i++) {
		if (fscanf(m, "%llu %llu", &a, &b) != 2 || b <= a)
			die("bad block map range");
		ranges[i][0] = a; ranges[i][1] = b;
	}
	fclose(m);
	uint64_t blocks = 0;
	for (uint64_t i = 0; i < nranges; i++)
		blocks += ranges[i][1] - ranges[i][0];
	if (blocks * bsize < size)
		die("block map shorter than the zip");
	fd = open(dev, O_RDONLY);
	if (fd < 0)
		die(dev);
}

static uint32_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

static void out(const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len) {
		ssize_t w = write(1, p, len);
		if (w <= 0)
			die("write");
		p += w; len -= w;
	}
}

int main(int argc, char **argv)
{
	enum { CHUNK = 1 << 20 };
	static uint8_t in[CHUNK], buf[CHUNK];

	if (argc != 3) {
		fprintf(stderr, "usage: gtaxl-unzip <zip|@block.map> <entry>\n");
		return 2;
	}
	open_source(argv[1]);

	/* end of central directory: in the last 64 KiB + 22 bytes */
	size_t tail = size < 65557 ? size : 65557;
	read_at(size - tail, buf, tail);
	const uint8_t *eocd = NULL;
	for (size_t i = tail - 22 + 1; i-- > 0; )
		if (le32(buf + i) == 0x06054b50) { eocd = buf + i; break; }
	if (!eocd)
		die("no end of central directory");
	uint32_t entries = le16(eocd + 10), cd_size = le32(eocd + 12), cd_off = le32(eocd + 16);
	if (cd_size > sizeof(in))
		die("central directory too big");
	read_at(cd_off, in, cd_size);

	const uint8_t *e = in;
	size_t name_len = strlen(argv[2]);
	for (uint32_t n = 0; n < entries; n++) {
		if (e + 46 > in + cd_size || le32(e) != 0x02014b50)
			die("bad central directory");
		uint32_t nlen = le16(e + 28), elen = le16(e + 30), clen = le16(e + 32);
		if (nlen == name_len && !memcmp(e + 46, argv[2], nlen))
			break;
		e += 46 + nlen + elen + clen;
		if (n + 1 == entries)
			die("entry not found");
	}
	uint32_t method = le16(e + 10), crc = le32(e + 16), csize = le32(e + 20);
	uint32_t usize = le32(e + 24), loc = le32(e + 42);
	uint8_t lh[30];
	read_at(loc, lh, 30);
	if (le32(lh) != 0x04034b50)
		die("bad local header");
	uint64_t data = (uint64_t)loc + 30 + le16(lh + 26) + le16(lh + 28);

	uLong got = crc32(0, Z_NULL, 0);
	uint64_t done = 0, written = 0;
	if (method == 0) {
		while (done < csize) {
			size_t c = csize - done < CHUNK ? csize - done : CHUNK;
			read_at(data + done, in, c);
			got = crc32(got, in, c);
			out(in, c);
			done += c;
		}
		written = done;
	} else if (method == 8) {
		z_stream z = { 0 };
		int r = Z_OK;

		if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
			die("inflateInit2");
		while (r != Z_STREAM_END) {
			if (!z.avail_in) {
				if (done == csize)
					die("deflate stream truncated");
				size_t c = csize - done < CHUNK ? csize - done : CHUNK;
				read_at(data + done, in, c);
				done += c;
				z.next_in = in; z.avail_in = c;
			}
			z.next_out = buf; z.avail_out = CHUNK;
			r = inflate(&z, Z_NO_FLUSH);
			if (r != Z_OK && r != Z_STREAM_END)
				die("inflate");
			size_t c = CHUNK - z.avail_out;
			got = crc32(got, buf, c);
			out(buf, c);
			written += c;
		}
		inflateEnd(&z);
	} else {
		die("unsupported compression method");
	}
	if (written != usize || got != crc)
		die("size or CRC-32 mismatch");
	return 0;
}
