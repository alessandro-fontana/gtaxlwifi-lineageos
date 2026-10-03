/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "GtaxlHealth.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

namespace aidl::android::hardware::health {

namespace {

constexpr const char* kChargerInputCurrentLimit =
        "/sys/class/power_supply/sm5703-charger/input_current_limit";

// Above the longest spurious unplug measured (2426 ms), with margin.
constexpr std::chrono::milliseconds kUnpluggedDebounce{5000};

template <typename D>
long long Ms(D d) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

}  // namespace

bool GtaxlHealth::DebouncedChargerOnline(bool online) {
    const auto now = Clock::now();
    if (online) {
        if (unplugged_since_) {
            LOG(WARNING) << "power back after " << Ms(now - *unplugged_since_)
                         << " ms: spurious unplug ignored";
            unplugged_since_.reset();
        }
        seen_online_ = true;
        return true;
    }

    // No power. If it was there before, the unplug is believed only after
    // kUnpluggedDebounce: until then, as far as the charger knows, the cable
    // is still there.
    if (!seen_online_) return false;
    if (!unplugged_since_) {
        unplugged_since_ = now;
        LOG(WARNING) << "power absent: confirming in " << kUnpluggedDebounce.count()
                     << " ms";
    }
    if (now - *unplugged_since_ < kUnpluggedDebounce) return true;

    LOG(WARNING) << "unplug confirmed after " << Ms(now - *unplugged_since_) << " ms";
    unplugged_since_.reset();
    seen_online_ = false;
    return false;
}

int GtaxlHealth::OnPrepareToWait() {
    int timeout = Health::OnPrepareToWait();
    if (!unplugged_since_) return timeout;

    auto left = Ms(*unplugged_since_ + kUnpluggedDebounce - Clock::now());
    int wait = left > 0 ? static_cast<int>(left) : 0;
    return timeout < 0 ? wait : std::min(timeout, wait);
}

void GtaxlHealth::UpdateHealthInfo(HealthInfo* health_info) {
    const bool online = health_info->chargerAcOnline || health_info->chargerUsbOnline;
    if (!online || health_info->maxChargingCurrentMicroamps > 0) return;

    std::string limit;
    int microamps;
    if (::android::base::ReadFileToString(kChargerInputCurrentLimit, &limit) &&
        ::android::base::ParseInt(::android::base::Trim(limit), &microamps, 1)) {
        health_info->maxChargingCurrentMicroamps = microamps;
    }
}

bool GtaxlChargerCallback::ChargerIsOnline() {
    return service_->DebouncedChargerOnline(ChargerCallback::ChargerIsOnline());
}

}  // namespace aidl::android::hardware::health
