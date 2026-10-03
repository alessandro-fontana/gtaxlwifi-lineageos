#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Records logcat on the CACHE partition, to capture boots that hang before
# the system becomes reachable and total freezes (frozen screen, no adb)
# that leave only a forced reboot.
#
# Why it is done this way and not otherwise, all verified on the device:
#
#  - ramoops is no use: on this device the DRAM retains nothing, not even
#    across a clean reboot, tested with a marker written to /dev/kmsg and
#    /sys/fs/pstore empty on return.
#
#  - androidboot.save_early_logs is no use: it dumps to /metadata at a point
#    of the boot that a hung boot never reaches, and the file stays at zero
#    bytes.
#
#  - the mount must be sync. The only way out of a hang is a forced power
#    off, which throws away the page cache: without MS_SYNCHRONOUS what
#    logcat wrote is not on the eMMC yet. And init's "mount" builtin can NOT
#    do sync: the word is not in mount_flags[]
#    (system/core/init/builtins.cpp) and would end up in the option string
#    passed to ext4, which ignores it. That is why the mount is done here.
#
#  - absolute paths: init does not guarantee a useful PATH, and a
#    "logcat: not found" inside nohup leaves no trace anywhere.
#    The cachelog- prefix tells these files apart from those of the old
#    diag-init wrapper, which are named boot-N-*.
#
#  - no "while read" loops around logcat: in toybox they cost a fork per
#    line, measured at 26 lines/s against 8300/s for a direct redirection.
#    The script forks once and then replaces itself with logcat.
#
# Additions of 08/09/2026, after a freeze that left no log:
#
#  - a full CACHE fails silently. "echo N > cachelog-count" succeeds
#    because the file already exists, while creating .info and .log does
#    not: from the afternoon of 07/09 to the evening of 08/09 the counter
#    went from 38 to 52 and not one byte was written. Now the last KEEP
#    boots are kept and FREE_MIN_KB free are required before starting,
#    deleting the oldest until there is enough.
#
#  - the files rotated by logcat (cachelog-N.log.1, .2, .3) were not touched
#    by any cleanup: the glob is cachelog-N.*, not cachelog-N.log.
#
#  - "mount || exit 1" failed if CACHE was already mounted: the service
#    could not be restarted by hand (start vendor.cache-logger -> "exited
#    with status 1" in 0.2 s). Now an existing mount is fine as long as it
#    is sync, and if a recorder is already running a second one is not
#    started.
#
#  - every outcome, good or bad, goes to /dev/kmsg: it shows up in dmesg, in
#    logcat's kernel buffer and therefore in the log itself.
#
# Restart by hand, after a cleanup or to resume recording:
#     adb shell start vendor.cache-logger
#     adb shell 'dmesg | grep "cache-logger:" | tail -3'
#

MP=/cache           # CACHE mounted from fstab since 27/09 (§65.12)
DEV=/dev/block/by-name/CACHE
DIR=$MP/diag
KEEP=2              # previous boots to keep, besides the current one
FREE_MIN_KB=40960   # logcat takes (ROT_N+1)*ROT_KB = 32 MB per boot, plus margin
ROT_KB=8192         # ~90 KB/s measured: 4 files of 8 MB are about six minutes
ROT_N=3             # rotated files BESIDES the active one

say() { echo "cache-logger: $*" > /dev/kmsg; }

if grep -q " $MP " /proc/mounts; then
    # Already mounted (manual restart): it only has to be sync.
    if ! grep " $MP " /proc/mounts | grep -qE '[ ,]sync[ ,]'; then
        /system/bin/mount -o remount,sync $MP || { say "remount sync of $MP failed"; exit 1; }
    fi
else
    /system/bin/mount -o rw,sync -t ext4 $DEV $MP || { say "mount $DEV on $MP failed"; exit 1; }
fi
mkdir -p $DIR || { say "mkdir $DIR failed"; exit 1; }

# Only one recorder at a time.
if pgrep -f "logcat -b all -v threadtime -f $DIR/" > /dev/null; then
    say "already running, not starting again"
    exit 0
fi

# A sequence number instead of the date: the clock may not be set yet, and
# a hung boot must not overwrite the previous one.
N=0
[ -f $DIR/cachelog-count ] && N=$(cat $DIR/cachelog-count)
N=$((N + 1))
echo $N > $DIR/cachelog-count || say "cachelog-count not writable"

# Two-step cleanup. First: remove everything older than the last KEEP boots
# (cachelog-N.info, .log, .log.1 ...). Second: if space is still not
# enough, remove the oldest one left, until it is.
free_kb() { set -- $(df -k $MP | tail -n 1); echo $4; }
oldest() { ls $DIR | sed -n 's/^cachelog-\([0-9][0-9]*\)\..*/\1/p' | sort -n | uniq | head -n 1; }

for f in $DIR/cachelog-*.*; do
    [ -e "$f" ] || break
    n=${f##*/cachelog-}; n=${n%%.*}
    [ "$n" -le $((N - KEEP - 1)) ] 2>/dev/null && rm -f "$f"
done

FREE=$(free_kb)
while [ "$FREE" -lt $FREE_MIN_KB ]; do
    o=$(oldest)
    [ -n "$o" ] && [ "$o" -lt $N ] || break
    rm -f $DIR/cachelog-$o.*
    say "$MP at $FREE KB free, removed cachelog-$o"
    FREE=$(free_kb)
done
if [ "$FREE" -lt $FREE_MIN_KB ]; then
    say "only $FREE KB free on $MP: not recording, clean $DIR by hand"
    exit 1
fi

# Header in a separate file: logcat -f opens its own for writing.
{
    echo "== cachelog $N"
    echo "== uptime: $(cat /proc/uptime)  date: $(date)  (the clock may not be set yet)"
    echo "== free on $MP: $FREE KB; keeping the last $KEEP boots"
    echo "== cmdline: $(cat /proc/cmdline)"
    echo "== uname: $(uname -a)"
} > $DIR/cachelog-$N.info || { say "cachelog-$N.info not writable"; exit 1; }

say "recording to $DIR/cachelog-$N.log, $FREE KB free"
exec /system/bin/logcat -b all -v threadtime -f $DIR/cachelog-$N.log -r $ROT_KB -n $ROT_N '*:V'
