#
# SPDX-FileCopyrightText: 2026 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),gtaxlwifi)

# gtaxl-gpt for the OTA package (releasetools.py, docs §72): it goes in the
# install/ directory of the target-files, which the updater extracts to
# /tmp/install. Static, because it runs in the recovery from /tmp.
include $(CLEAR_VARS)
LOCAL_MODULE := gtaxl-gpt
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_SRC_FILES := ota/gtaxl-gpt.c
LOCAL_CFLAGS := -Wall -Wextra -Werror
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES := libc
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/install/bin
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice
include $(BUILD_EXECUTABLE)

endif
