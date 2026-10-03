#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#
# The device product (§55). Until 25/09 the build target was
# lineage_Generic_arm64 and everything specific to this tablet lived in a
# patch to device/mainline/generic; now the generic device is inherited as
# is and the differences live here.

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_tablet_wifionly.mk)

# Inherit from device
$(call inherit-product, device/samsung/gtaxlwifi/device.mk)

PRODUCT_NAME := lineage_gtaxlwifi
PRODUCT_DEVICE := gtaxlwifi
PRODUCT_BRAND := samsung
# The same value libinit derives at runtime from the device tree model for
# the brand ("Samsung Galaxy Tab A 10.1"): this way brand and manufacturer
# match even without the libinit patch of §47.
PRODUCT_MANUFACTURER := Samsung
# The model code, as on stock and on the official LineageOS Samsung devices:
# it is the key into Google's device catalogue, which with the marketing
# name shows only "Android" in the account (§71.5).
PRODUCT_MODEL := SM-T580
