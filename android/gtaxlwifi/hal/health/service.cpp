/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Like the main.cpp of the generic tree's HAL
 * (device/mainline/generic/hals/health), without the Cuttlefish branch for
 * battery-less machines: this tablet has one, and the HAL is GtaxlHealth.
 */

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/ChargerUtils.h>
#include <health/utils.h>

#include "GtaxlHealth.h"

using aidl::android::hardware::health::GtaxlChargerCallback;
using aidl::android::hardware::health::GtaxlHealth;
using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::charger::ChargerModeMain;

static constexpr const char* gInstanceName = "default";
static constexpr std::string_view gChargerArg{"--charger"};

int main(int argc, char** argv) {
    const bool charger = argc >= 2 && argv[1] == gChargerArg;

    // In charging mode logd is not running: messages go to the kernel
    // buffer, where the charger writes too and where they can be read with
    // dmesg as soon as Android is up, in the same session (§42.10).
    if (charger) android::base::InitLogging(argv, android::base::KernelLogger);

    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    auto binder = ndk::SharedRefBase::make<GtaxlHealth>(gInstanceName, std::move(config));

    if (charger) {
        // The spurious-unplug filter lives in the charger callback.
        return ChargerModeMain(binder, std::make_shared<GtaxlChargerCallback>(binder));
    }

    LOG(INFO) << "Starting health HAL.";
    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
