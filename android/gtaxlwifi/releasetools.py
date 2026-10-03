#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
"""OTA package steps of gtaxlwifi (SM-T580), on top of the generic non-A/B
package of ota_from_target_files (docs §72):

- before the addon backup, ota/gtaxl-ota.sh prepare: a factory partition
  table becomes the Android 16 layout (docs §65, §69), and on an update the
  addon backup goes on USERDATA instead of RAM;
- boot.img (the loader with kernel, DTB and ramdisk, docs §73) goes to BOOT
  (p9) through the generic script: "/boot" is BOOT in the recovery fstab;
- at the end, ota/gtaxl-ota.sh finish.
"""

OTA_SH = "/tmp/install/bin/gtaxl-ota.sh"


def FullOTA_InstallBegin(info):
  # The install/ directory is unpacked by the generic script only after this
  # point; the device steps need it before the backup.
  info.script.AppendExtra('package_extract_dir("install", "/tmp/install");')
  info.script.AppendExtra(
      'set_metadata_recursive("/tmp/install", "uid", 0, "gid", 0, '
      '"dmode", 0755, "fmode", 0755);')
  info.script.AppendExtra(
      'run_program("/system/bin/sh", "%s", "prepare") == 0 || '
      'abort("E3004: gtaxlwifi: preparing the partitions failed, see '
      '/tmp/gtaxl-ota.log");' % OTA_SH)


def FullOTA_InstallEnd(info):
  info.script.AppendExtra('run_program("/system/bin/sh", "%s", "finish");' % OTA_SH)
