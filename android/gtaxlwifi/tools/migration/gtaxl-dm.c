// SPDX-License-Identifier: Apache-2.0
/*
 * gtaxl-dm: point the device-mapper devices of super's logical partitions at
 * the extents of the super that update-binary has just written (docs §69).
 *
 * The recovery maps the logical partitions (map_logical_partitions()) before
 * it runs a package, from the super it finds. The installer then rewrites
 * super whole, and the partitions move: the old mappings would make an addon
 * installed in the same session, or the backuptool restore, write over the
 * wrong sectors. The official updater remaps after its dynamic partition
 * operations; this does the same with the extents lpmake gave the new image,
 * listed by build-zip.sh (one extent per partition, lpmake of an empty super).
 *
 *   gtaxl-dm <super block device> <map file>
 *   gtaxl-dm --remove <map file>
 *
 * --remove deletes the devices named in the map, where they exist: after a
 * fresh install the partition table changes only on the next boot, and
 * until then nothing may write through the old mappings.
 *
 * Map file lines: <name> <first sector in super> <sectors>. A device that
 * exists gets the new table loaded and swapped in by the resume (the same
 * dm-N, so /dev/block/mapper/<name> stays valid); a missing one is created,
 * and ueventd makes its /dev/block/mapper link. Tables are writable, as the
 * recovery's own (force_writable in userdebug builds).
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <linux/dm-ioctl.h>

#define BUF_SIZE 16384

static int ctl;

static struct dm_ioctl *dm_init(void *buf, const char *name)
{
	struct dm_ioctl *io = buf;

	memset(buf, 0, BUF_SIZE);
	io->version[0] = DM_VERSION_MAJOR;
	io->version[1] = 0;
	io->version[2] = 0;
	io->data_size = BUF_SIZE;
	io->data_start = sizeof(*io);
	snprintf(io->name, sizeof(io->name), "%s", name);
	return io;
}

static int dm_call(unsigned long cmd, void *buf)
{
	return ioctl(ctl, cmd, buf);
}

static int map_one(const char *name, unsigned long long first,
		   unsigned long long sectors, dev_t super)
{
	static uint64_t buf[BUF_SIZE / 8];
	struct dm_ioctl *io;
	struct dm_target_spec *t;
	char *params;
	int exists;

	io = dm_init(buf, name);
	exists = dm_call(DM_DEV_STATUS, buf) == 0;
	if (!exists && errno != ENXIO) {
		fprintf(stderr, "%s: status: %s\n", name, strerror(errno));
		return -1;
	}
	if (!exists) {
		io = dm_init(buf, name);
		if (dm_call(DM_DEV_CREATE, buf)) {
			fprintf(stderr, "%s: create: %s\n", name, strerror(errno));
			return -1;
		}
	}

	io = dm_init(buf, name);
	io->target_count = 1;
	t = (struct dm_target_spec *)((char *)buf + sizeof(*io));
	t->sector_start = 0;
	t->length = sectors;
	snprintf(t->target_type, sizeof(t->target_type), "linear");
	params = (char *)(t + 1);
	snprintf(params, BUF_SIZE - sizeof(*io) - sizeof(*t), "%u:%u %llu",
		 major(super), minor(super), first);
	if (dm_call(DM_TABLE_LOAD, buf)) {
		fprintf(stderr, "%s: table load: %s\n", name, strerror(errno));
		return -1;
	}

	/* resume swaps the loaded table in, suspending first if needed */
	io = dm_init(buf, name);
	if (dm_call(DM_DEV_SUSPEND, buf)) {
		fprintf(stderr, "%s: resume: %s\n", name, strerror(errno));
		return -1;
	}
	printf("%s: %s, sectors %llu+%llu of %u:%u, dm-%u\n", name,
	       exists ? "remapped" : "created", first, sectors,
	       major(super), minor(super), minor(io->dev));
	return 0;
}

static int remove_all(FILE *f)
{
	static uint64_t buf[BUF_SIZE / 8];
	char name[DM_NAME_LEN];
	unsigned long long first, sectors;
	int ret = 0;

	while (fscanf(f, "%127s %llu %llu", name, &first, &sectors) == 3) {
		dm_init(buf, name);
		if (!dm_call(DM_DEV_REMOVE, buf)) {
			printf("%s: removed\n", name);
		} else if (errno != ENXIO) {
			fprintf(stderr, "%s: remove: %s\n", name, strerror(errno));
			ret = 1;
		}
	}
	return ret;
}

int main(int argc, char **argv)
{
	char name[DM_NAME_LEN], link[PATH_MAX];
	unsigned long long first, sectors;
	struct stat st;
	FILE *f;
	int n = 0, i;

	if (argc == 3 && !strcmp(argv[1], "--remove")) {
		f = fopen(argv[2], "r");
		if (!f) {
			perror(argv[2]);
			return 1;
		}
		ctl = open("/dev/device-mapper", O_RDWR | O_CLOEXEC);
		if (ctl < 0) {
			perror("/dev/device-mapper");
			return 1;
		}
		return remove_all(f);
	}
	if (argc != 3) {
		fprintf(stderr, "usage: %s <super block device> <map file>\n"
			"       %s --remove <map file>\n", argv[0], argv[0]);
		return 2;
	}
	if (stat(argv[1], &st) || !S_ISBLK(st.st_mode)) {
		fprintf(stderr, "%s: not a block device\n", argv[1]);
		return 1;
	}
	f = fopen(argv[2], "r");
	if (!f) {
		perror(argv[2]);
		return 1;
	}
	ctl = open("/dev/device-mapper", O_RDWR | O_CLOEXEC);
	if (ctl < 0) {
		perror("/dev/device-mapper");
		return 1;
	}
	while (fscanf(f, "%127s %llu %llu", name, &first, &sectors) == 3) {
		if (!sectors || map_one(name, first, sectors, st.st_rdev))
			return 1;
		n++;
	}
	if (!feof(f) || !n) {
		fprintf(stderr, "%s: malformed\n", argv[2]);
		return 1;
	}

	/* the links of created devices come from ueventd: wait for them */
	rewind(f);
	while (fscanf(f, "%127s %llu %llu", name, &first, &sectors) == 3) {
		snprintf(link, sizeof(link), "/dev/block/mapper/%s", name);
		for (i = 0; i < 100 && access(link, F_OK); i++)
			usleep(100000);
		if (access(link, F_OK)) {
			fprintf(stderr, "%s: no link after 10 s\n", link);
			return 1;
		}
	}
	return 0;
}
