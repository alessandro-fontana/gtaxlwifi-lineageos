#!/system/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# LineageOS Recovery on the factory partition table (§65.12): there is no
# misc partition until the install zip migrates the table, and the recovery
# prints "Failed to clear BCB message ... misc: No such file or directory" at
# every start and install. Give it a misc in RAM instead: a 1 MiB file in /tmp on a
# loop device. Nothing reads the BCB on that table (S-BOOT looks for misc,
# which is not there), so this only silences the errors; nothing is written
# to the eMMC. On the migrated table the real misc exists and this does
# nothing.
M=/dev/block/by-name/misc
[ -e $M ] && exit 0
truncate -s 1048576 /tmp/gtaxl-misc.img || exit 0
L=$(losetup -f --show /tmp/gtaxl-misc.img) || exit 0
ln -s "$L" $M
