/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <android/binder_auto_utils.h>

namespace gtaxlwifi::touch {

/*
 * The two vendor.lineage.touch interfaces this tablet implements have the
 * same shape — a boolean in a sysfs attribute of the stmfts driver — and
 * only the file name changes. It all lives here.
 */
bool exists(const char* path);
::ndk::ScopedAStatus readFlag(const char* path, bool* out);
::ndk::ScopedAStatus writeFlag(const char* path, bool value);

/*
 * Fixed paths, not searched for: this tablet's touchscreen is always i2c-2
 * address 0x49 (exynos7870-gtaxlwifi.dts, &i2c1 -> bus 2). Constant also
 * because that is what allows labelling them in genfs_contexts without
 * wildcards.
 */
inline constexpr const char* kKeySwapPath =
        "/sys/devices/platform/soc@0/13840000.i2c/i2c-2/2-0049/key_swap";
inline constexpr const char* kKeyDisablePath =
        "/sys/devices/platform/soc@0/13840000.i2c/i2c-2/2-0049/key_disable";

}  // namespace gtaxlwifi::touch
