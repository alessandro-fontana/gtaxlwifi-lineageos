#!/sbin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Device steps of the LineageOS OTA package for gtaxlwifi (SM-T580), run by
# the updater through releasetools.py (docs §72):
#
#   gtaxl-ota.sh prepare   before the addon backup and the dynamic partitions
#   gtaxl-ota.sh finish    at the end, after the addon restore
#
# prepare: a factory partition table (stock or LineageOS 21) becomes the
# Android 16 layout (docs §65, §69): the GPT gtaxl-gpt computes, the kernel
# rereads it, and the heads of metadata, misc and USERDATA are zeroed so that
# Android formats them. super is then empty,
# and the updater writes its metadata from unsparse_super_empty.img. On the
# migrated table (an update) nothing changes, and the addon backup goes on
# USERDATA: backuptool keeps it in /tmp/backupdir, and GApps are ~1.1 GB, an
# out-of-memory panic in the RAM of this 2 GB tablet (§69). The directory is
# a folder of /data, or, if Android has not formatted it yet, a scratch ext4
# whose head is zeroed again by finish.
#
# Needs only the recovery's toybox and gtaxl-gpt (installed next to this
# script).

DISK=/dev/block/mmcblk0
BIN=${0%/*}
T=/tmp/gtaxl-ota
LOG=/tmp/gtaxl-ota.log
BK_MNT=/tmp/gtaxl-bk
BK_DEV=/dev/block/by-name/USERDATA
# metadata, misc, USERDATA of the Android 16 layout (gtaxl-gpt.c, V2)
HEADS="8265728 8298496 8300544"

log() { echo "gtaxl-ota: $*" | tee -a "$LOG"; }
die() { log "ERROR: $*"; exit 1; }

migrate() {
	log "factory partition table: writing the Android 16 layout, all data is erased"
	chmod 755 "$BIN/gtaxl-gpt"
	"$BIN/gtaxl-gpt" migrate "$T/gpt.bin" "$SECTORS" "$T/primary.bin" "$T/backup.bin" >>"$LOG" 2>&1 \
		|| die "computing the new table, nothing written"
	dd if="$T/backup.bin" of=$DISK bs=512 seek=$((SECTORS - 33)) count=33 2>>"$LOG" \
		|| die "writing the backup table"
	dd if="$T/primary.bin" of=$DISK bs=512 seek=1 count=33 2>>"$LOG" \
		|| die "writing the primary table"
	sync
	dd if=$DISK of="$T/readback.bin" bs=512 skip=1 count=33 2>>"$LOG"
	cmp -s "$T/readback.bin" "$T/primary.bin" || die "the table does not read back the same"
	for lba in $HEADS; do
		dd if=/dev/zero of=$DISK bs=512 seek=$lba count=2048 2>>"$LOG" || die "zeroing LBA $lba"
	done
	sync
	blockdev --rereadpt $DISK 2>>"$LOG" \
		|| die "the kernel did not reread the table: reboot to recovery and install again"
	# ueventd makes the by-name links of the new partitions
	i=0
	while [ ! -e /dev/block/by-name/super ] || [ ! -e /dev/block/by-name/boot ]; do
		i=$((i + 1))
		[ $i -gt 50 ] && die "no by-name links for the new partitions: reboot to recovery and install again"
		sleep 0.2
	done
	[ "$(cat /sys/class/block/mmcblk0p22/start)" = 8300544 ] \
		|| die "the kernel still sees the old partitions: reboot to recovery and install again"
	log "partition table migrated"
}

backup_store() {
	mkdir -p $BK_MNT /tmp/backupdir
	if ! mount -t ext4 $BK_DEV $BK_MNT 2>>"$LOG"; then
		# anything but a wiped head (4 KiB of zeros) is data: leave it alone
		[ "$(head -c 4096 $BK_DEV | tr -d '\000' | head -c 1 | wc -c)" = 0 ] || return 1
		touch "$T/scratch"
		mke2fs -F -q -t ext4 $BK_DEV >>"$LOG" 2>&1 || return 1
		mount -t ext4 $BK_DEV $BK_MNT 2>>"$LOG" || return 1
	fi
	mkdir -p $BK_MNT/gtaxl-addon-backup && mount -o bind $BK_MNT/gtaxl-addon-backup /tmp/backupdir
}

backup_store_release() {
	grep -q " $BK_MNT " /proc/mounts || return 0
	umount /tmp/backupdir 2>/dev/null
	rm -rf $BK_MNT/gtaxl-addon-backup
	umount $BK_MNT
	[ -e "$T/scratch" ] && dd if=/dev/zero of=$BK_DEV bs=1048576 count=1 2>>"$LOG"
	rm -f "$T/scratch"
	sync
}

case $1 in
prepare)
	rm -rf "$T"; mkdir -p "$T"
	# nothing of the eMMC may stay mounted while the table changes or the
	# backup store is set up
	for m in /data /sdcard /cache /metadata; do
		umount "$m" 2>/dev/null
	done
	SECTORS=$(cat /sys/block/mmcblk0/size)
	dd if=$DISK of="$T/gpt.bin" bs=512 skip=1 count=33 2>>"$LOG" || die "reading the GPT"
	chmod 755 "$BIN/gtaxl-gpt"
	STATUS=$("$BIN/gtaxl-gpt" status "$T/gpt.bin" "$SECTORS"); RC=$?
	log "partition table: $STATUS"
	case $RC in
	0)	migrate ;;
	3)	backup_store || die "no room on USERDATA for the addon backup, nothing written" ;;
	*)	die "partition table not recognised, nothing written" ;;
	esac
	;;
finish)
	backup_store_release
	;;
*)
	die "usage: $0 prepare|finish"
	;;
esac
exit 0
