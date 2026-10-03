/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * gtaxl-gpt: computes the GPT of gtaxlwifi (SM-T580) to go from the factory
 * layout to the Android 16 layout and back.
 *
 * The Android 16 layout (docs §65, §69) turns the 3400 MiB SYSTEM into a
 * "super" partition of the same size, holding the logical partitions, and
 * adds the physical boot, metadata and misc as p23-p25 at the start of the
 * old USERDATA area; USERDATA starts 145 MiB later. The entries with the
 * tablet's own data (p1-p18), CACHE and HIDDEN keep their factory names and
 * LBAs: Android and the loader find partitions by name (§65.8).
 *
 * Here v1 and v2 name the layouts of §65 and §69, not the older ones the
 * docs call v1 and v2 (§33, 18.1).
 * The first version (v1, 27/09/2026, §65) had a 3127 MiB super, p23-p25 at
 * the end of the old SYSTEM and USERDATA as shipped: too small for addons
 * (§69). A v1 table is recognised, migrated to v2 (a clean install: USERDATA
 * moves) and reverted.
 *
 * Works on files only: reads the primary GPT (LBA 1-33, 16896 bytes) and
 * writes a new primary and backup. It never opens the disk: the caller does
 * the dd, after checking the exit status.
 *
 *   gtaxl-gpt status  <primary.bin> <disk-sectors>
 *   gtaxl-gpt migrate <primary.bin> <disk-sectors> <new-primary> <new-backup>
 *   gtaxl-gpt revert  <primary.bin> <disk-sectors> <new-primary> <new-backup>
 *
 * `status` prints "factory" (exit 0), "migrated" (exit 3), "migrated-v1"
 * (exit 4) or why the table is not recognised (exit 1). `migrate` accepts a
 * factory or v1 table and writes v2, `revert` accepts v2 or v1.
 *
 * The table keeps Samsung's signatures (§33.2): HeaderSize 512, disk GUID
 * "ANDROID MMC DISK", every entry GUID "ANDROID " + the first 8 characters of
 * its name, the same type GUID for all entries. The backup goes at LBA
 * <disk-sectors> - 33: entry array first, then the header.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 512
#define N_ENTRIES 128
#define ENTRY_SIZE 128
#define ARRAY_SIZE (N_ENTRIES * ENTRY_SIZE) /* 16384 = 32 sectors */
#define TABLE_SIZE (SECTOR + ARRAY_SIZE)    /* 16896 = 33 sectors */

static const uint8_t TYPE_GUID[16] = {0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
				      0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7};
static const char DISK_GUID[16] = "ANDROID MMC DISK";

struct part { const char *name; uint64_t first, last; };

/* p1-p22 as shipped: gpt/gpt-fabbrica-main.bin. USERDATA's last LBA depends
 * on the chip capacity and is not checked. */
static const struct part FACTORY[22] = {
	{"BOTA0", 8192, 16383},        {"BOTA1", 16384, 24575},
	{"EFS", 24576, 65535},         {"CPEFS", 65536, 81919},
	{"m9kefs1", 81920, 90111},     {"m9kefs2", 90112, 98303},
	{"m9kefs3", 98304, 106495},    {"PARAM", 106496, 122879},
	{"BOOT", 122880, 188415},      {"RECOVERY", 188416, 266239},
	{"OTA", 266240, 282623},       {"CDMA-RADIO", 282624, 290815},
	{"RADIO", 290816, 471039},     {"TOMBSTONES", 471040, 473087},
	{"DNT", 473088, 475135},       {"PERSISTENT", 475136, 476159},
	{"PERSDATA", 476160, 500735},  {"RESERVED2", 500736, 507903},
	{"SYSTEM", 507904, 7471103},   {"CACHE", 7471104, 7880703},
	{"HIDDEN", 7880704, 8003583},  {"USERDATA", 8003584, 0},
};

/* 19 becomes super (from the start of the old SYSTEM), 23-25 are added and
 * 22 (USERDATA) may start later. v2: super is all of the old SYSTEM, boot
 * (128 MiB), metadata and misc where USERDATA started. */
#define FIRST_ADDED 23
#define N_ADDED 3
struct layout { uint64_t super_last, userdata_first; struct part added[N_ADDED]; };
static const struct layout V1 = {6911999, 8003584, {
	{"boot", 6912000, 7436287},
	{"metadata", 7436288, 7469055},
	{"misc", 7469056, 7471103},
}};
static const struct layout V2 = {7471103, 8300544, {
	{"boot", 8003584, 8265727},
	{"metadata", 8265728, 8298495},
	{"misc", 8298496, 8300543},
}};

static uint32_t crc_table[256];
static void crc_init(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++)
			c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
		crc_table[i] = c;
	}
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
	uint32_t c = 0xffffffffu;
	while (n--)
		c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = v >> (8 * i); }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = v >> (8 * i); }

static uint8_t *entry(uint8_t *t, int n) { return t + SECTOR + (n - 1) * ENTRY_SIZE; }

/* names are UTF-16LE in 72 bytes; ASCII only here */
static int name_is(const uint8_t *e, const char *name)
{
	size_t l = strlen(name);
	for (size_t i = 0; i < 36; i++) {
		uint16_t c = e[56 + 2 * i] | e[57 + 2 * i] << 8;
		if (c != (i < l ? (uint8_t)name[i] : 0))
			return 0;
	}
	return 1;
}
static void set_name(uint8_t *e, const char *name)
{
	memset(e + 56, 0, 72);
	for (size_t i = 0; name[i]; i++)
		e[56 + 2 * i] = name[i];
}
static void set_guid(uint8_t *e, const char *name)
{
	memset(e + 16, 0, 16);
	memcpy(e + 16, "ANDROID ", 8);
	for (int i = 0; i < 8 && name[i]; i++)
		e[24 + i] = name[i];
}
static void new_entry(uint8_t *e, const struct part *p)
{
	memset(e, 0, ENTRY_SIZE);
	memcpy(e, TYPE_GUID, 16);
	set_guid(e, p->name);
	put64(e + 32, p->first);
	put64(e + 40, p->last);
	set_name(e, p->name);
}

static const char *reason;
static int fail(const char *r) { reason = r; return 0; }

/* header and CRCs; `disk` is the number of sectors */
static int header_ok(uint8_t *t, uint64_t disk)
{
	uint8_t *h = t;
	if (memcmp(h, "EFI PART", 8)) return fail("no EFI PART signature");
	if (le32(h + 8) != 0x10000) return fail("unexpected GPT revision");
	if (le32(h + 12) != SECTOR) return fail("HeaderSize is not 512: not a Samsung table");
	uint32_t c = le32(h + 16);
	put32(h + 16, 0);
	uint32_t r = crc32(h, SECTOR);
	put32(h + 16, c);
	if (r != c) return fail("bad header CRC");
	if (le64(h + 24) != 1) return fail("MyLBA is not 1: not the primary table");
	if (le64(h + 32) != disk - 1) return fail("AlternateLBA is not the last sector of the disk");
	if (le64(h + 40) != 34) return fail("FirstUsableLBA is not 34");
	if (le64(h + 48) != disk - 34) return fail("unexpected LastUsableLBA for this disk");
	if (memcmp(h + 56, DISK_GUID, 16)) return fail("disk GUID is not Samsung's");
	if (le64(h + 72) != 2) return fail("entry array is not at LBA 2");
	if (le32(h + 80) != N_ENTRIES || le32(h + 84) != ENTRY_SIZE) return fail("unexpected entry count or size");
	if (crc32(t + SECTOR, ARRAY_SIZE) != le32(h + 88)) return fail("bad entry array CRC");
	return 1;
}

static int factory_entry_ok(uint8_t *t, int n)
{
	const struct part *f = &FACTORY[n - 1];
	uint8_t *e = entry(t, n);
	if (memcmp(e, TYPE_GUID, 16)) return fail("unexpected entry type");
	if (!name_is(e, f->name)) return fail("unexpected name of a factory entry");
	if (le64(e + 32) != f->first) return fail("unexpected start of a factory entry");
	if (f->last && le64(e + 40) != f->last) return fail("unexpected end of a factory entry");
	return 1;
}

static int empty_from(uint8_t *t, int from)
{
	for (int n = from; n <= N_ENTRIES; n++)
		for (int i = 0; i < ENTRY_SIZE; i++)
			if (entry(t, n)[i]) return fail("extra entries present");
	return 1;
}

static int is_factory(uint8_t *t, uint64_t disk)
{
	if (!header_ok(t, disk)) return 0;
	for (int n = 1; n <= 22; n++)
		if (!factory_entry_ok(t, n)) return 0;
	uint64_t u = le64(entry(t, 22) + 40);
	if (u < 8003584 || u > disk - 34) return fail("USERDATA runs past the disk");
	return empty_from(t, 23);
}

static int is_layout(uint8_t *t, uint64_t disk, const struct layout *l)
{
	if (!header_ok(t, disk)) return 0;
	for (int n = 1; n <= 22; n++) {
		uint8_t *e = entry(t, n);
		if (n == 19) {
			if (!name_is(e, "super") || le64(e + 32) != 507904 ||
			    le64(e + 40) != l->super_last)
				return fail("p19 is not super");
		} else if (n == 22) {
			if (memcmp(e, TYPE_GUID, 16) || !name_is(e, "USERDATA") ||
			    le64(e + 32) != l->userdata_first)
				return fail("p22 is not USERDATA where the layout puts it");
			if (le64(e + 40) <= l->userdata_first || le64(e + 40) > disk - 34)
				return fail("USERDATA runs past the disk");
		} else if (!factory_entry_ok(t, n)) {
			return 0;
		}
	}
	for (int i = 0; i < N_ADDED; i++) {
		uint8_t *e = entry(t, FIRST_ADDED + i);
		if (!name_is(e, l->added[i].name) || le64(e + 32) != l->added[i].first ||
		    le64(e + 40) != l->added[i].last)
			return fail("p23-p25 are not boot, metadata and misc");
	}
	return empty_from(t, FIRST_ADDED + N_ADDED);
}

/* recompute the primary's CRCs and derive the backup from it */
static void finish(uint8_t *p, uint8_t *b, uint64_t disk)
{
	put32(p + 88, crc32(p + SECTOR, ARRAY_SIZE));
	put32(p + 16, 0);
	put32(p + 16, crc32(p, SECTOR));

	memcpy(b, p + SECTOR, ARRAY_SIZE);
	uint8_t *h = b + ARRAY_SIZE;
	memcpy(h, p, SECTOR);
	put64(h + 24, disk - 1);   /* MyLBA */
	put64(h + 32, 1);          /* AlternateLBA */
	put64(h + 72, disk - 33);  /* PartitionEntryLBA */
	put32(h + 16, 0);
	put32(h + 16, crc32(h, SECTOR));
}

/* from a factory or v1 table */
static void migrate(uint8_t *t)
{
	uint8_t *e = entry(t, 19);
	set_name(e, "super");
	set_guid(e, "super");
	put64(e + 40, V2.super_last);
	put64(entry(t, 22) + 32, V2.userdata_first);
	for (int i = 0; i < N_ADDED; i++)
		new_entry(entry(t, FIRST_ADDED + i), &V2.added[i]);
}

/* from a v1 or v2 table */
static void revert(uint8_t *t)
{
	uint8_t *e = entry(t, 19);
	set_name(e, "SYSTEM");
	set_guid(e, "SYSTEM");
	put64(e + 40, FACTORY[18].last);
	put64(entry(t, 22) + 32, FACTORY[21].first);
	memset(entry(t, FIRST_ADDED), 0, N_ADDED * ENTRY_SIZE);
}

static int read_table(const char *f, uint8_t *b)
{
	FILE *fp = fopen(f, "rb");
	if (!fp) { perror(f); return 0; }
	size_t n = fread(b, 1, TABLE_SIZE, fp);
	int extra = fgetc(fp) != EOF;
	fclose(fp);
	if (n != TABLE_SIZE || extra) {
		fprintf(stderr, "%s: expected exactly %d bytes (LBA 1-33)\n", f, TABLE_SIZE);
		return 0;
	}
	return 1;
}
static int write_table(const char *f, const uint8_t *b)
{
	FILE *fp = fopen(f, "wb");
	if (!fp || fwrite(b, 1, TABLE_SIZE, fp) != TABLE_SIZE || fclose(fp)) {
		perror(f);
		return 0;
	}
	return 1;
}

int main(int argc, char **argv)
{
	static uint8_t t[TABLE_SIZE], b[TABLE_SIZE];
	crc_init();
	if (argc < 4) goto usage;
	uint64_t disk = strtoull(argv[3], NULL, 10);
	if (disk < V2.userdata_first + 34) { fprintf(stderr, "invalid disk sector count\n"); return 2; }
	if (!read_table(argv[2], t)) return 2;

	int fac = is_factory(t, disk);
	const char *why_not_factory = reason;
	int mig = is_layout(t, disk, &V2);
	const char *why_not_migrated = reason;
	int v1 = is_layout(t, disk, &V1);
	const char *why_not_v1 = reason;

	if (!strcmp(argv[1], "status") && argc == 4) {
		if (fac) { puts("factory"); return 0; }
		if (mig) { puts("migrated"); return 3; }
		if (v1) { puts("migrated-v1"); return 4; }
		printf("unknown: as factory, %s; as migrated, %s; as v1, %s\n",
		       why_not_factory, why_not_migrated, why_not_v1);
		return 1;
	}
	if (argc != 6) goto usage;
	if (!strcmp(argv[1], "migrate")) {
		if (!fac && !v1) {
			fprintf(stderr, "neither the factory table (%s) nor v1 (%s)\n", why_not_factory, why_not_v1);
			return 1;
		}
		migrate(t);
	} else if (!strcmp(argv[1], "revert")) {
		if (!mig && !v1) {
			fprintf(stderr, "not a migrated table: %s; as v1, %s\n", why_not_migrated, why_not_v1);
			return 1;
		}
		revert(t);
	} else {
		goto usage;
	}
	finish(t, b, disk);
	/* final check: the result must read back as the expected layout */
	int ok = !strcmp(argv[1], "migrate") ? is_layout(t, disk, &V2) : is_factory(t, disk);
	if (!ok) { fprintf(stderr, "invalid result (%s): not writing\n", reason); return 1; }
	if (!write_table(argv[4], t) || !write_table(argv[5], b)) return 2;
	return 0;
usage:
	fprintf(stderr, "usage: gtaxl-gpt status|migrate|revert <primary.bin> <disk-sectors> [<new-primary> <new-backup>]\n");
	return 2;
}
