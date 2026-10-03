#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# DEVICE_PATH is not redefined: the generic BoardConfig uses it in lazy
# assignments (SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(DEVICE_PATH)/...)
# and it must stay device/mainline/generic, as its device.mk sets it.
GTAXL_PATH := device/samsung/gtaxlwifi

# Kernel: this device's board.mk instead of the generic kernel.
# Set here, before the include, it makes unnecessary the two exports that the
# lunch of lineage_Generic_arm64 required (SETUP-interno.md §5).
MAINLINE_GENERIC_KERNEL_BOARDCONFIG_MK := $(GTAXL_PATH)/kernels/exynos7870/board.mk

# Inherit from parent
include device/mainline/generic/Generic_arm64/BoardConfig.mk

# Graphics (Mesa): only Mali T830 (Midgard) via Panfrost. Vulkan: panvk is
# for Valhall, it does not exist on Midgard; swrast/lavapipe is used,
# consistent with SwiftShader, which the generic device provides as a
# fallback. After the include, because the generic device adds its lists
# with +=.
BOARD_MESA3D_GALLIUM_DRIVERS := panfrost
BOARD_MESA3D_VULKAN_DRIVERS := swrast

# Kernel modules: the same scan as the generic device, minus the modules
# that must not be loaded from the ramdisk (see the script). Only read in
# the recipes of vendor/lineage/build/tasks/kernel.mk, so the override after
# the include takes effect.
BOOT_KERNEL_MODULES_FINDER := $(GTAXL_PATH)/configs/kernel/boot_kernel_modules_finder.sh

# SELinux: this hardware's labels and domains.
BOARD_VENDOR_SEPOLICY_DIRS += $(GTAXL_PATH)/sepolicy/vendor

# Wi-Fi (§53): the AOSP default HAL, with libwifi-hal-gtaxl loaded from
# /vendor/etc/wifi/vendor_hals. The combinations are the ones the driver
# really supports: ath10k (ath10k_tlv_if_comb) allows STA and AP together on
# the same channel, and with the QCA9377 firmware on two channels as well
# (the _qcs_ combination, measured: client on 7, hotspot on 6, 4, 9, 11).
# P2P is the P2P_DEVICE created by wpa_supplicant (p2p-dev-wlan0, §53.6).
WIFI_HAL_INTERFACE_COMBINATIONS := {{{STA}, 1}, {{AP}, 1}, {{P2P}, 1}}

# Charging control (vendor.lineage.health, §68, §76). The charger's boolean
# node charging_enabled (kernel 0190), the same state as charge_behaviour,
# which the HAL cannot parse ("[auto] inhibit-charge").
$(call soong_config_set,lineage_health,charging_control_charging_path,/sys/class/power_supply/sm5703-charger/charging_enabled)
$(call soong_config_set,lineage_health,charging_control_charging_enabled,1)
$(call soong_config_set,lineage_health,charging_control_charging_disabled,0)

# Boot (§73): Samsung's S-BOOT boots BOOT (p9) or RECOVERY (p10). Both hold
# bootloader/loader, which carries the kernel, the DTB, the ramdisk and the
# command line, and boots Linux; bootimg/mkbootimg.mk packs them, inside the
# build. boot is therefore BOOT, 32 MiB; the "boot" partition (p23), where
# U-Boot used to read an ext4, is no longer needed and is kept only so the
# partition table does not change. RECOVERY is 38 MiB, and the real limit is
# checked by gtaxl_bootimg.py. 128 MiB here: add_img_to_target_files still
# builds from the ramdisk a recovery-two-step.img in AOSP format (55 MiB,
# only for two-step packages, which this device does not use) and wants it
# to fit the partition.
BOARD_BOOTIMAGE_PARTITION_SIZE := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 134217728
BOARD_CUSTOM_BOOTIMG := true
BOARD_CUSTOM_BOOTIMG_MK := $(GTAXL_PATH)/bootimg/mkbootimg.mk
# The non-A/B package wants a recovery from which to regenerate the
# device's one (HasRecoveryPatch): the full image in /vendor/etc. LineageOS
# does not rewrite it at boot (persist.vendor.recovery_update=false).
BOARD_USES_FULL_RECOVERY_IMAGE := true

# OTA package (§72): the generic device skips it, the LineageOS servers
# build it with ota_from_target_files; the device steps are in
# releasetools.py.
TARGET_SKIP_OTA_PACKAGE := false
TARGET_RELEASETOOLS_EXTENSIONS := $(GTAXL_PATH)

# AVB (§72): sign_target_files_apks signs the APEXes with avbtool and looks
# for it in the build metadata, which only record it with AVB enabled.
# Nothing verifies it: the loader does not read vbmeta, the fstab has no "avb",
# and vbmeta declares verification disabled (--flags 3: verity disabled, as
# the charter requires for system on userdebug). boot and recovery are
# chained with their own keys, as AVB requires for non-A/B devices; boot has
# its footer (bootimg/mkbootimg.mk), the recovery for S-BOOT does not, and
# it is not in vbmeta.
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
BOARD_AVB_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_BOOT_ALGORITHM := SHA256_RSA4096
BOARD_AVB_BOOT_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_BOOT_ROLLBACK_INDEX_LOCATION := 1
BOARD_AVB_RECOVERY_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_RECOVERY_ALGORITHM := SHA256_RSA4096
BOARD_AVB_RECOVERY_ROLLBACK_INDEX := $(PLATFORM_SECURITY_PATCH_TIMESTAMP)
BOARD_AVB_RECOVERY_ROLLBACK_INDEX_LOCATION := 2

TARGET_RECOVERY_FSTAB := $(GTAXL_PATH)/configs/fstab/recovery.fstab

# /cache is the CACHE partition (p20), as on LineageOS devices that have
# one (§65.12): the Updater and uncrypt write to /cache/recovery, and the
# recovery reads from there on the same partition. With the type declared,
# the build creates /cache as a directory instead of the link to
# /data/cache (system/core/rootdir/create_root_structure.mk). cache.img is
# not installed.
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_CACHEIMAGE_PARTITION_SIZE := 209715200

# Partitions for addons (GApps) from recovery, like official devices (§69):
# logical system, product and system_ext in ext4, writable, with the
# LineageOS reserved space. The generic device puts everything in a single
# erofs system, read-only and full. product in the minimal variant
# (1.19 GB: MindTheGapps 16 uses ~1.1): the full one (1.96 GB) does not fit
# in the 3400 MiB of super. No BOARD_EXT4_SHARE_DUP_BLOCKS: with shared
# blocks ext4 can only be mounted read-only.
TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_SYSTEM_EXT := system_ext
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_PRODUCTIMAGE_MINIMAL_PARTITION_RESERVED_SIZE := true
include vendor/lineage/config/BoardConfigReservedSize.mk

# super (p19), as tools/migration/build-zip.sh writes it: the whole former
# SYSTEM, 6963200 sectors (layout v2, §69), one group with everything minus
# 4 MiB (liblp metadata and alignment). Once declared, the build checks that
# the images fit (check-all-partition-sizes) and puts
# ro.boot.dynamic_partitions=true in vendor, which the recovery reads to map
# the logical partitions before an addon (map_logical_partitions()).
BOARD_SUPER_PARTITION_SIZE := 3565158400
BOARD_SUPER_PARTITION_GROUPS := gtaxl
BOARD_GTAXL_SIZE := 3560964096
BOARD_GTAXL_PARTITION_LIST := system product system_ext vendor vendor_dlkm system_dlkm

# The generic device's libinit (§71.5): with set_properties_from=both it
# reads the device tree "model" and overwrites at runtime the brand and
# model of every partition ("Galaxy Tab A 10.1"), useful on PCs and virtual
# machines. Here the model is the build's one (SM-T580,
# lineage_gtaxlwifi.mk), the key by which Google recognises the device. In
# BoardConfig because it comes after the generic device.mk, which sets
# "both". The ro.soc.* properties the same function derived from the device
# tree are in device.mk.
$(call soong_config_set,mainline_common_libinit,set_properties_from,none)

# Proprietary
-include vendor/samsung/gtaxlwifi/BoardConfigVendor.mk
