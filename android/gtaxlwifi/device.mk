#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# Samsung Galaxy Tab A 10.1 (2016) Wi-Fi, SM-T580, Exynos 7870 (§55).
#
# The device inherits device/mainline/generic as is and adds its own
# differences here. The precedence rules this file relies on:
#   - PRODUCT_COPY_FILES and PRODUCT_PACKAGE_OVERLAYS: for the same
#     destination the line that comes FIRST wins. That is why the generic
#     device is inherited at the bottom of the file: placed at the top, it
#     won (measured, §55: audio at 44.1 kHz and
#     config_canInternalDisplayHostDesktops=true);
#   - a file from the generic device is replaced without removing it from
#     the generic device: in /odm when its consumer also looks there,
#     otherwise with `overrides` (see Android.bp).
#
# DEVICE_PATH is not redefined: it belongs to the generic device (see
# BoardConfig.mk).
GTAXL_PATH := device/samsung/gtaxlwifi

# Optional HALs from device/mainline/common: read by optional/options.mk,
# which the generic device.mk includes as soon as it is inherited (at the
# bottom of this file).
TARGET_POWER_HAL := perfmgr-lineage
TARGET_SENSORS_HAL := iio
# No vibration motor: the downstream Wi-Fi defconfig lacks SEC_VIB, the LTE
# one has it (the sec_vib DTS node is in the common dtsi).
# With the glodroid HAL, apps believed they were vibrating.
TARGET_HAS_VIBRATOR := false
# Thermal HAL (hal/thermal, §76): Linaro's frontend (hardware/linaro/
# libpm, from mainline.xml), fixed, on top of its libthermal. It talks to
# the kernel over thermal netlink (fixups.config) and reads the thresholds
# from /vendor/etc/thermal.json (§68). Every entry is Optional: the HAL
# throws an exception if a mandatory entry is missing, system_server waits
# for it forever and boot stays on the animation (happened on 29/09). And
# the names are the netlink ones, truncated to THERMAL_NAME_LENGTH - 1 = 19
# characters: devfreq-11400000.gpu arrives as devfreq-11400000.gp.
PRODUCT_PACKAGES += android.hardware.thermal-service.gtaxlwifi
# Suspend (§68.15, §68.22): the generic device disables it by installing
# suspend_blocker (its line is made `?=` by device_mainline_generic.patch).
# The firmware does not offer SYSTEM_SUSPEND: "mem" is s2idle. Wi-Fi
# disconnects on entry and reconnects on resume: no WoWLAN, because the
# SDIO firmware in WoW hangs on the first delivery to the host (§68.22).
TARGET_SUPPORTS_SUSPEND := true

# "Show taps" (§68.27): the generic device turns it on at every boot with a
# script in the su domain, meant for PCs and virtual machines. Not here (the
# line is made optional by device_mainline_generic.patch).
TARGET_GENERIC_SHOW_TOUCHES := false

# Fixed hardware: no hardware_detect (§70). The generic device runs it at
# every boot to choose graphics and audio HALs, APEXes and DRM nodes, and on
# this tablet it needed the su domain (the ro.hardware.* properties are
# reserved to init) and the writable /vendor overlay for the VINTF
# fragments. The same choices, read from its log of 1/10, are below in
# build.prop, in the cmdline (APEX) and in ueventd (DRM node); the VINTF
# fragments and the hwc3 .rc are installed by the packages themselves
# (generic device option, .patches/device_mainline_generic.patch).
TARGET_GENERIC_HARDWARE_DETECT := false
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.audio.primary=tinyhal \
    ro.hardware.egl=mesa \
    ro.hardware.gralloc=minigbm_upstream \
    ro.hardware.vulkan=panfrost \
    ro.opengles.version=196609 \
    ro.vendor.graphics.card.name=exynos \
    ro.vendor.graphics.render.name=panfrost \
    vendor.hwc.drm.device=/dev/dri/card0

# SoC (Android 12 CDD): the same values libinit derived from the device
# tree compatible before §71.5.
PRODUCT_VENDOR_PROPERTIES += \
    ro.soc.manufacturer=samsung \
    ro.soc.model=exynos7870
# vendor.minigbm.device cannot be set from build.prop (vendor_init has no
# set on vendor_minigbm_prop): minigbm tries the render nodes in order,
# renderD128 is closed by ueventd and it takes renderD129, panfrost
# (verified 1/10).
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/configs/thermal/thermal.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal.json

# LineageOS charging control (charge limit, §68): the HAL writes
# charging_enabled of sm5703-charger (kernel 0167, 0190), configured in
# BoardConfig.mk. With charging off (inhibit-charge) the buck stays on: the
# tablet runs from the charger and the battery stays idle, i.e. BYPASS mode.
PRODUCT_PACKAGES += \
    vendor.lineage.health-service.default
# TI's libcamera in external/libcamera, with the simple pipeline and the software ISP (§57)
TARGET_CAMERA_PROVIDER_HAL := libcamera
# Rear camera with autofocus and flash; drop the capabilities the HAL lacks (§57).
PRODUCT_PACKAGES += \
    android.hardware.camera.flash-autofocus.prebuilt.xml \
    gtaxlwifi-camera-features.xml

# Hardware video codecs: MFC via external/v4l2_codec2 (§58). The example
# configuration in device/mainline declares 4K and secure codecs the 7870
# does not have: ours is here.
TARGET_MEDIA_C2_HAL := v4l2_codec2
TARGET_V4L2_CODEC2_USE_EXAMPLE_CONFIGURATION := false
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/configs/media/media_codecs_c2.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_c2.xml \
    $(GTAXL_PATH)/configs/media/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml
# Pool mask: the example one (0xf50000) plus GRALLOC (bit 17). Without it,
# ByteBuffer decoding takes a basic pool, whose blocks have no stable id,
# and v4l2_codec2 cannot tell which V4L2 buffer to bind them to (§75.8).
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.v4l2_codec2.decode_concurrent_instances=4 \
    ro.vendor.v4l2_codec2.encode_concurrent_instances=4 \
    debug.stagefright.c2-poolmask=0xf70000
# v4l2_codec2 only advertises the codecs enabled here: one at a time, after
# testing on the tablet, together with the entry in media_codecs_c2.xml
# (§58.3). Each one needs s5p-mfc to speak the stateful API v4l2_codec2
# expects (§58.4): the decoder from patch 0145 (§58.5), the encoder from
# 0146 (§58.6), HEVC and VP8 from 0154 (§58.10), their encoders from 0155
# and v4l2_codec2 0005 (§58.11), MPEG-4 and H.263 both ways from v4l2_codec2
# 0007 and kernel 0186-0187 (§75). VP9: the 7870 firmware does not have
# it, it stays in software. Enabled before the driver, Android picks them and boot fails.
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.v4l2_codec2.decoder.supported.h263=true \
    ro.vendor.v4l2_codec2.decoder.supported.h264=true \
    ro.vendor.v4l2_codec2.decoder.supported.hevc=true \
    ro.vendor.v4l2_codec2.decoder.supported.mpeg4=true \
    ro.vendor.v4l2_codec2.decoder.supported.vp8=true \
    ro.vendor.v4l2_codec2.encoder.supported.h263=true \
    ro.vendor.v4l2_codec2.encoder.supported.h264=true \
    ro.vendor.v4l2_codec2.encoder.supported.hevc=true \
    ro.vendor.v4l2_codec2.encoder.supported.mpeg4=true \
    ro.vendor.v4l2_codec2.encoder.supported.vp8=true

# Camera metadata FMQs: the HAL defaults (1 MB), written here because
# without them the HAL falls back to ro.camera.*, system properties with no
# context that a vendor service cannot read (a default_prop denial on every
# open, §57.21).
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.camera.req.fmq.size=1048576 \
    ro.vendor.camera.res.fmq.size=1048576

# Restart the codec and camera services when the graphics allocator
# restarts (§58.8)
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/etc/init/gtaxl-allocator-restart.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/gtaxl-allocator-restart.rc

# Starting tuning for libcamera's software ISP (§57)
PRODUCT_PACKAGES += \
    libcamera_ipa_simple_uncalibrated.yaml \
    libcamera_ipa_simple_s5k4h5yc.yaml \
    libcamera_ipa_simple_sr259.yaml

# Proprietary: GPS blobs and Wi-Fi/Bluetooth firmware (extract-files.py)
$(call inherit-product, vendor/samsung/gtaxlwifi/gtaxlwifi-vendor.mk)

# Our own signing keys (vendor/gtaxl-keys, outside git: see SETUP-interno.md);
# without them, test-keys
-include vendor/gtaxl-keys/keys.mk

# 10" tablet, sw 800dp: product-qualified strings go to the tablet variant,
# not the phone one (about_settings, power_dialog and 167 others in
# frameworks/base). It affects aapt2, so it requires installclean.
PRODUCT_CHARACTERISTICS := tablet

# Audio (§24): TinyHAL opens /vendor/etc/audio.<ro.product.vendor.device>.xml,
# and the device is now called gtaxlwifi: the codec_probe that chose this
# file inside audio.generic.xml is no longer needed. The 48 kHz primary
# policy with a FAST output (§68.16) replaces the generic one (see the top).
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/configs/audio/audio.gtaxlwifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio.gtaxlwifi.xml \
    $(GTAXL_PATH)/configs/audio/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml

# Doze: no AOD (§28.2). Replaces the generic device's AodDefaultOnOverlay.
PRODUCT_PACKAGES += \
    GtaxlNoAodOverlay

# Wi-Fi PNO with the screen off (§68.21): ath10k scheduled scan (kernel
# 0174) and the resource that makes the framework use it.
PRODUCT_PACKAGES += \
    GtaxlWifiOverlay

# Fstab: encrypted /data and /metadata mounted by the second stage, /misc,
# zram (§37, §41, §17.12). Installed in /odm/etc, which libfstab reads
# before /vendor/etc (Android.bp).
PRODUCT_PACKAGES += \
    fstab.gtaxlwifi \
    fstab.generic_init.addon.gtaxl

# ueventd: permissions of this hardware's nodes. /odm/etc/ueventd.rc is
# imported by /system/etc/ueventd.rc and nothing else uses it here.
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/configs/init/ueventd.gtaxlwifi.rc:$(TARGET_COPY_OUT_ODM)/etc/ueventd.rc

# LineageOS Recovery (§65.12): the USB controller for adb and sideload, and
# an in-RAM misc on the factory partition table (gtaxl-misc.sh).
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/etc/init/gtaxl-recovery.rc:$(TARGET_COPY_OUT_RECOVERY)/root/system/etc/init/gtaxl-recovery.rc \
    $(GTAXL_PATH)/rootdir/etc/gtaxl-misc.sh:$(TARGET_COPY_OUT_RECOVERY)/root/system/etc/gtaxl-misc.sh

# /metadata mounted at fs, before post-fs (§65.6): goes together with the
# entry without latemount in fstab.gtaxlwifi.
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/etc/init/gtaxl-metadata.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/gtaxl-metadata.rc

# Boot splash (§43): fills the black gap between the DECON reset and the
# SurfaceFlinger modeset with the LineageOS animation. It goes in the
# ramdisk, launched by generic_init in first stage, before the modules and
# SwitchRoot.
PRODUCT_PACKAGES += \
    bootsplash \
    bootsplash_vendor

# The same splash, shown again when leaving charging mode (§43.11): there
# the kernel has been up for a while and nobody draws between the charger
# and SurfaceFlinger.
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/etc/init/gtaxl-bootsplash.rc:$(TARGET_COPY_OUT_VENDOR)/etc/overlay-system_ext-etc-init/gtaxl-bootsplash.rc

# Health HAL (§55): the AOSP default one, with the filter for spurious
# cable unplugs in charging mode (§42.13) that used to be a patch to
# system/core/healthd. Replaces android.hardware.health-service.generic.
PRODUCT_PACKAGES += \
    android.hardware.health-service.gtaxlwifi

# Charging mode (§42.9): without this, GRSurfaceDrm::Create falls back to
# DRM_FORMAT_RGB565 while gr_clear() writes a uint32_t per pixel ignoring
# pixel_bytes: it overruns the mapping and the service dies of SIGSEGV on
# the first draw. BGRX_8888 is the byte order of DRM_FORMAT_XRGB8888.
PRODUCT_VENDOR_PROPERTIES += \
    ro.minui.pixel_format=BGRX_8888

# Input (§50). The mainline stmfts driver names the two capacitive keys
# KEY_MENU and KEY_BACK after the ST reference design; on this tablet's
# glass they are, as measured, recents on the left and back on the right.
# EventHub picks the file by the input device NAME, "stmfts".
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/usr/keylayout/stmfts.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/stmfts.kl

# vendor.lineage.touch HAL (§52). The two interfaces LineageOS's Buttons
# section expects from a device with capacitive keys:
#   IKeySwapper  "Swap capacitive buttons"   -> key_swap    (kernel 0109)
#   IKeyDisabler "Enable on-screen nav bar"  -> key_disable (kernel 0110)
PRODUCT_PACKAGES += \
    vendor.lineage.touch-service.gtaxlwifi

# Wi-Fi HAL (§53): the AOSP default one, which loads libwifi-hal-gtaxl
# (hal/wifi). With the HAL the framework knows the driver's combinations
# (hotspot together with Wi-Fi, BoardConfig.mk) and takes the code path of
# official devices.
PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    libwifi-hal-gtaxl

# Wi-Fi Direct (§53.6): the HAL returns p2p-dev-wlan0 and wpa_supplicant
# creates an nl80211 P2P_DEVICE interface by itself. Measured with a phone.
PRODUCT_VENDOR_PROPERTIES += \
    wifi.direct.interface=p2p-dev-wlan0

# GPS (§54): Broadcom BCM4752 on ttySAC0. gpsd and the legacy HAL
# gps.default.so are blobs (vendor/samsung/gtaxlwifi); the AIDL HAL that
# exposes them to the framework is ours (hal/gnss), and so is libwrappergps,
# which since 2/10 replaces the empty blob from the SM-P610 firmware
# (§75.9). gtaxl-gps-rfkill.sh links the file gpsd uses to power the chip,
# and powers it off again after "rfkill unblock all".
PRODUCT_PACKAGES += \
    android.hardware.gnss-service.gtaxlwifi \
    libwrappergps

PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/bin/gtaxl-gps-rfkill.sh:$(TARGET_COPY_OUT_VENDOR)/bin/gtaxl-gps-rfkill.sh \
    $(GTAXL_PATH)/rootdir/etc/gps/gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/gps.xml \
    $(GTAXL_PATH)/rootdir/etc/init/gps.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/gps.rc

# Overlays: cover, auto brightness, GNSS, hardware keys, desktop.
PRODUCT_PACKAGE_OVERLAYS += \
    $(GTAXL_PATH)/overlay

# Permissions: the hardware profile the device declares about itself,
# i.e. what Play receives at check-in (§47).
#
# The generic device's pc_core_hardware.xml is a PC profile: it does not
# declare android.hardware.touchscreen and declares
# android.hardware.type.pc, and on this tablet the two together gave
# `am get-config` = `notouch` and almost every app "not supported" by Play.
# The touchscreen is there and measured (`getevent -pl` on stmfts:
# ABS_MT_SLOT 0-9, ten fingers). The second file removes type.pc, for
# which there is no stock file.
#
# Bluetooth LE (§49.7), GPS (§54), Wi-Fi Direct (§53.6), accelerometer and
# light sensor (§48, in sensorservice since 21/09 but never declared: Play
# hid the apps that require them): each declared after measuring it. The
# removed features are in gtaxl_core_hardware.xml.
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    $(GTAXL_PATH)/rootdir/etc/permissions/gtaxl_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/gtaxl_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml

# perfmgr-lineage Power HAL: optional/power-hal_perfmgr-lineage/product.mk
# asks for the powerhint.json module, which the device must define
# (Android.bp).

# Sensors: the IIO HAL in polling mode (§48).
#
# LIS2HH12 is declared in the device tree **without an IRQ**: the st_accel
# driver creates scan_elements/ anyway, and the HAL, which prefers trigger
# mode when it finds them (enumeration.c, enumerate_sensors), would open a
# buffer that no trigger fills. Without an interrupt line someone has to do
# the periodic sampling, and here the HAL does it by reading
# in_accel_*_raw.
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.iio.accel.quirks=no-trig \
    ro.vendor.iio.accel.name=LIS2HH12 \
    ro.vendor.iio.accel.vendor=STMicroelectronics

# Light sensor: the CM3323 exposes RGB and clear channels, not an
# illuminance channel. The HAL (hardware_intel_sensors-iio.patch, the RGBC
# catalogue entry) follows `clear`, and the value that reaches Android is
# the channel's **raw count**, not lux: the auto-brightness curve is
# written in counts (overlay, §49.6). Since kernel 0191 it has a buffer,
# hence the /dev/iio:device* node the HAL opens; with no IRQ, like the
# accelerometer, it is read by polling (no-trig).
PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.iio.intensity.quirks=no-trig \
    ro.vendor.iio.intensity.name=CM3323 \
    ro.vendor.iio.intensity.vendor=Capella_Microsystems

# Entropy seed kept across boots (§43.6): without it, every boot loses
# 1.27 s waiting for the CRNG. It is not diagnostics, it goes in user
# builds too.
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/bin/gtaxl-entropy-seed.sh:$(TARGET_COPY_OUT_VENDOR)/bin/gtaxl-entropy-seed.sh \
    $(GTAXL_PATH)/rootdir/etc/init/gtaxl-entropy-seed.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/gtaxl-entropy-seed.rc

# Diagnostics: logcat on CACHE for boots that hang before becoming
# reachable (§7.1). Enabled by androidboot.cachelogging=1.
ifneq ($(TARGET_BUILD_VARIANT),user)
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/rootdir/bin/cache-logger.sh:$(TARGET_COPY_OUT_VENDOR)/bin/cache-logger.sh \
    $(GTAXL_PATH)/rootdir/etc/init/cache-logger.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/cache-logger.rc
endif

# Bluetooth: the QCA9377 (ROME) answers the APCF sub-command
# READ_EXTENDED_FEATURES with the bare OCF (0x0157 instead of 0xfd57) and
# the stack aborts in hci_layer.cc:251, with a com.android.bluetooth crash
# loop. Basic APCF works (filtering_support: 1), so only reading the
# extended features is disabled.
PRODUCT_VENDOR_PROPERTIES += \
    bluetooth.le.disable_apcf_extended_features=1

# Graphics, tuned for the Mali-T830 MC1 (§17, §21, §45).
#
# SurfaceFlinger: hardware_detect turns blur on without looking at the GPU;
# with fully client composition Kawase costs ~13% on SystemUI percentiles
# (p50 53->46 ms, p90 85->73 ms over 4 alternating runs).
#
# Overscroll stretch: HWUI renders it as a full-size layer plus a
# full-screen AGSL shader, 8-9 Mcycles/frame. UniformScale is a matrix
# scale, and stretch_uniform_skips_layer also removes the RenderLayer that
# requiresLayer() did not remove: Settings 8.22 -> 5.37 Mcycles/frame
# (§45.4). Sysprop from frameworks_base.patch.
#
# drm_hwcomposer also offers scaled layers to the planes, and the 7870 DECON
# has no per-window scaler: without scale_with_gpu the atomic test fails
# with -ENOTSUPP and the whole scene goes to the GPU (§21.4).
PRODUCT_VENDOR_PROPERTIES += \
    ro.surface_flinger.supports_background_blur=0 \
    debug.hwui.stretch_effect_behavior=uniform_scale \
    debug.hwui.stretch_uniform_skips_layer=true \
    vendor.hwc.drm.scale_with_gpu=1

# Inherit from parent. At the bottom, not at the top: see the start of the file.
$(call inherit-product, device/mainline/generic/Generic_arm64/device.mk)

# Compressed APEXes, the AOSP default that the generic device turns off
# (§69): ext4 system does not compress, and the APEXes amount to 457 MiB.
# Compressed they take ~190 MiB in system, and apexd decompresses them into
# /data on first boot. The generic device uses ?=
# (.patches/device_mainline_generic.patch): inherited products are included
# after this file, and a := of theirs would win.
OVERRIDE_PRODUCT_COMPRESSED_APEX := true

# Dynamic partitions (§69): the dm tables are created by generic_init, but
# recovery and the addons look for /dev/block/mapper only with
# ro.boot.dynamic_partitions=true. The super sizes are in BoardConfig.mk.
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Updater (§65.12): the update server is LineageOS's default one, as on
# every official device (§72.2). An unofficial build is updated with
# "Local update" or by sideload.

# boot.img: the generic device does not build one (it boots from GRUB);
# here it is what S-BOOT starts from BOOT: the loader with the kernel, DTB,
# ramdisk and cmdline (bootloader/loader, §73), built by bootimg/mkbootimg.mk
# (§72)
PRODUCT_BUILD_BOOT_IMAGE := true

# The OTA package (releasetools.py, §72): gtaxl-ota.sh and gtaxl-gpt in
# install/bin, which the updater extracts to /tmp/install like backuptool.sh
PRODUCT_PACKAGES += \
    gtaxl-gpt
PRODUCT_COPY_FILES += \
    $(GTAXL_PATH)/ota/gtaxl-ota.sh:install/bin/gtaxl-ota.sh
