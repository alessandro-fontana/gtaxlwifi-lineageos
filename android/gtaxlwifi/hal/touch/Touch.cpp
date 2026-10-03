/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Touch.h"

#include "SysfsFlag.h"

namespace aidl::vendor::lineage::touch {

using ::gtaxlwifi::touch::kKeyDisablePath;
using ::gtaxlwifi::touch::kKeySwapPath;
using ::gtaxlwifi::touch::readFlag;
using ::gtaxlwifi::touch::writeFlag;

::ndk::ScopedAStatus KeySwapper::getEnabled(bool* _aidl_return) {
    return readFlag(kKeySwapPath, _aidl_return);
}

::ndk::ScopedAStatus KeySwapper::setEnabled(bool enabled) {
    return writeFlag(kKeySwapPath, enabled);
}

::ndk::ScopedAStatus KeyDisabler::getEnabled(bool* _aidl_return) {
    return readFlag(kKeyDisablePath, _aidl_return);
}

::ndk::ScopedAStatus KeyDisabler::setEnabled(bool enabled) {
    return writeFlag(kKeyDisablePath, enabled);
}

}  // namespace aidl::vendor::lineage::touch
