/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/touch/BnKeyDisabler.h>
#include <aidl/vendor/lineage/touch/BnKeySwapper.h>

namespace aidl::vendor::lineage::touch {

/*
 * Which of the two capacitive keys is the left one is a property of the
 * panel, not of the chip: the controller only says "key 0" and "key 1"
 * (§50.2). This one swaps the two keycodes.
 */
class KeySwapper : public BnKeySwapper {
  public:
    ::ndk::ScopedAStatus getEnabled(bool* _aidl_return) override;
    ::ndk::ScopedAStatus setEnabled(bool enabled) override;
};

/*
 * Turns off scanning of the two keys in the controller. It is the hardware
 * half of LineageOS's "Enable on-screen nav bar" checkbox: with the
 * on-screen bar enabled the capacitive keys are a duplicate and must be
 * silenced (§50.10). The home key is not involved, it is on gpio-keys.
 */
class KeyDisabler : public BnKeyDisabler {
  public:
    ::ndk::ScopedAStatus getEnabled(bool* _aidl_return) override;
    ::ndk::ScopedAStatus setEnabled(bool enabled) override;
};

}  // namespace aidl::vendor::lineage::touch
