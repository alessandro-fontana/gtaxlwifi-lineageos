#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Sources (proprietary-files.txt, §72.2): public Samsung firmware, i.e. the
# device image (T580XXS5CTK1) and, for the 64-bit GPS the stock firmware
# lacks, those of the Galaxy A8 2016 (A810SKSS2CTI1) and the Tab S6 Lite
# (P610XXS2EVJ2); the QCA9377 files from linux-firmware. Extract from a
# directory with the files at their paths, or from a tablet running
# LineageOS (adb):
#
#   ./extract-files.py <directory>
#

from extract_utils.fixups_blob import (
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

blob_fixups: blob_fixups_user_type = {
    # libicuuc and libgui: no symbol used (nm on the 340 imported symbols,
    # all resolved without them). The former does not exist on the vendor
    # side, the latter would drag in half the graphics stack for nothing.
    # /data/system/gps/ -> /data/vendor/gps/: same length, and the HAL and
    # gpsd stay in vendor data instead of system data.
    # The first two fixups are the ones the LOS 21 port had made by hand on
    # the A8 file to get it to start (commit 1d286f7 of
    # android_device_samsung_gtaxl-common): SSLv3_client_method ->
    # SSLv23_method, same length with the rest zeroed, and dropping the
    # dependency on android.hidl.base@1.0. On the already-patched file they
    # find nothing to change.
    'vendor/bin/hw/gpsd': blob_fixup()
        .binary_regex_replace(b'SSLv3_client_method', b'SSLv23_method\x00\x00\x00\x00\x00\x00')
        .remove_needed('android.hidl.base@1.0.so')
        .remove_needed('libicuuc.so')
        .remove_needed('libgui.so')
        .binary_regex_replace(b'/data/system/gps/', b'/data/vendor/gps/'),
    # The SONAME is that of the original Samsung file
    # (gps.samsungT0EVOLight64android.so), and Soong's ELF check wants the
    # file name.
    'vendor/lib64/hw/gps.default.so': blob_fixup()
        .fix_soname()
        .binary_regex_replace(b'/data/system/gps/', b'/data/vendor/gps/'),
}  # fmt: skip

module = ExtractUtilsModule(
    'gtaxlwifi',
    'samsung',
    blob_fixups=blob_fixups,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
