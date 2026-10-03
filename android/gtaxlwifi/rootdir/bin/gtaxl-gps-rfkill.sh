#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# The file through which gpsd powers the BCM4752 on and off (GpioNStdbyPath
# in gps.xml). The kernel describes the GPS_EN pin as rfkill-gpio (patch
# 0116): an rfkill state accepts "1" (on) and "0" (off), i.e. exactly what
# gpsd writes. But the rfkillN index depends on the order in which
# Bluetooth, Wi-Fi and GPS register, and gps.xml wants a fixed path: this
# script creates it as a symlink in /data/vendor/gps.
#
# The starting path is the platform device's, which is stable (the "gps"
# node of the device tree): under rfkill/ there is a single rfkillN.
#
# Runs as gps with CAP_NET_ADMIN (gps.rc), which the kernel requires to write
# an rfkill state; messages reach dmesg through stdio_to_kmsg.
#
# It also turns the chip back off: init.generic.rc does "rfkill unblock all"
# in early-init, after rfkill-gpio registered it blocked (default-blocked).

DIR=/sys/devices/platform/gps/rfkill
LINK=/data/vendor/gps/nstdby

for r in $DIR/rfkill*; do
    [ -e "$r/state" ] || continue
    echo 0 > "$r/state"
    ln -sf "$r/state" $LINK
    echo "gtaxl-gps-rfkill: $LINK -> $r/state, chip off"
    exit 0
done

echo "gtaxl-gps-rfkill: no rfkill in $DIR, gpsd will not be able to power the chip"
exit 1
