#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# This tablet's boot images (BOARD_CUSTOM_BOOTIMG_MK, included by
# build/make/core/Makefile). Samsung's S-BOOT, which stays in place, boots
# BOOT (p9) or RECOVERY (p10) as a Samsung boot image, but ignores the
# image's command line and rejects ramdisks above 14 MiB. The "kernel" of
# both is therefore bootloader/loader (§73), compiled here with the kernel's
# clang, followed by the real kernel (LZ4), the DTB, the ramdisk and the
# command line: the loader puts them in place, completes the DTB and boots
# Linux.
#
# With BOARD_CUSTOM_BOOTIMG := true the two images end up in
# BOOTABLE_IMAGES/ of the target-files, which the releasetools use as they
# are, even after signing.

GTAXL_BOOTIMG := $(GTAXL_PATH)/bootimg/gtaxl_bootimg.py
GTAXL_LOADER_DIR := $(GTAXL_PATH)/bootloader/loader
GTAXL_LOADER_SRCS := $(wildcard $(GTAXL_LOADER_DIR)/*.c $(GTAXL_LOADER_DIR)/*.S \
	$(GTAXL_LOADER_DIR)/*.h $(GTAXL_LOADER_DIR)/include/*.h) \
	$(GTAXL_LOADER_DIR)/linker.lds $(GTAXL_LOADER_DIR)/build.sh
GTAXL_LOADER := $(call intermediates-dir-for,PACKAGING,gtaxl_loader)/loader.bin
GTAXL_SAMSUNG_DT := $(GTAXL_PATH)/bootloader/stub-dt.img
# kernel.mk's KERNEL_OUT is not defined yet here: the same path
GTAXL_DTB := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/arch/arm64/boot/dts/exynos/exynos7870-gtaxlwifi-decon.dtb
GTAXL_LZ4 := $(HOST_OUT_EXECUTABLES)/lz4
# The generic device's single ramdisk (ramdisk.img plus ramdisk-custom.img).
# Spelled out: its variable, INSTALLED_RAMDISK_ALL_COMBINED_TARGET, comes from
# device/mainline/generic/build/tasks/01-ramdisk.mk, which core/Makefile
# includes after this file, so as a prerequisite it would expand to nothing
# and boot.img would keep packing a stale ramdisk (docs §77.6).
GTAXL_RAMDISK := $(PRODUCT_OUT)/ramdisk-all-combined.img

$(GTAXL_LOADER): $(GTAXL_LOADER_SRCS)
	$(call pretty,"Target gtaxlwifi loader: $@")
	$(hide) CLANG_DIR=$(TARGET_KERNEL_CLANG_PATH) $(GTAXL_LOADER_DIR)/build.sh $@ $(BUILD_TOP)

# boot: for BOOT, with the generic device's single ramdisk and the Android
# command line. With the AVB footer at the end of the partition: vbmeta
# chains boot (BoardConfig.mk), and its digest is computed over this image
# too. Nothing verifies it at boot; S-BOOT reads only the part the header
# describes.
$(INSTALLED_BOOTIMAGE_TARGET): $(GTAXL_BOOTIMG) $(GTAXL_LOADER) $(INSTALLED_KERNEL_TARGET) \
		$(GTAXL_RAMDISK) $(GTAXL_PATH)/cmdline.txt \
		$(GTAXL_SAMSUNG_DT) $(GTAXL_LZ4) $(AVBTOOL) $(BOARD_AVB_BOOT_KEY_PATH)
	$(call pretty,"Target boot image: $@")
	$(hide) python3 $(GTAXL_BOOTIMG) loader --loader $(GTAXL_LOADER) \
		--kernel $(INSTALLED_KERNEL_TARGET) --dtb $(GTAXL_DTB) \
		--ramdisk $(GTAXL_RAMDISK) \
		--cmdline $(GTAXL_PATH)/cmdline.txt --dt $(GTAXL_SAMSUNG_DT) \
		--lz4 $(GTAXL_LZ4) --output $@
	$(hide) $(AVBTOOL) add_hash_footer --image $@ \
		--partition_size $(BOARD_BOOTIMAGE_PARTITION_SIZE) --partition_name boot \
		--key $(BOARD_AVB_BOOT_KEY_PATH) --algorithm $(BOARD_AVB_BOOT_ALGORITHM) \
		--rollback_index $(BOARD_AVB_BOOT_ROLLBACK_INDEX)

# recovery: for RECOVERY, with the LineageOS recovery ramdisk. The 38 MiB
# limit of RECOVERY is checked by the script
# (BOARD_RECOVERYIMAGE_PARTITION_SIZE is larger, see BoardConfig.mk)
$(INSTALLED_RECOVERYIMAGE_TARGET): $(GTAXL_BOOTIMG) $(GTAXL_LOADER) $(INSTALLED_KERNEL_TARGET) \
		$(recovery_ramdisk) $(GTAXL_PATH)/recovery-cmdline.txt \
		$(GTAXL_SAMSUNG_DT) $(GTAXL_LZ4)
	$(call pretty,"Target recovery image: $@")
	$(hide) python3 $(GTAXL_BOOTIMG) loader --recovery --loader $(GTAXL_LOADER) \
		--kernel $(INSTALLED_KERNEL_TARGET) --dtb $(GTAXL_DTB) \
		--ramdisk $(recovery_ramdisk) \
		--cmdline $(GTAXL_PATH)/recovery-cmdline.txt --dt $(GTAXL_SAMSUNG_DT) \
		--lz4 $(GTAXL_LZ4) --output $@
