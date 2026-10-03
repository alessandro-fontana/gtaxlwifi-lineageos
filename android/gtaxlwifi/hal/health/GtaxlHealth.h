/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <health-impl/ChargerUtils.h>
#include <health-impl/Health.h>

#include <chrono>
#include <optional>

namespace aidl::android::hardware::health {

/*
 * The AOSP default Health HAL, plus the filter for spurious cable unplugs in
 * charging mode (§42.13, §55).
 *
 * On this tablet the "power present" state flaps during the first seconds of
 * charging mode: spurious unplugs of 331 to 2426 ms, measured, always within
 * the first twenty-five seconds. The AOSP charger took them at face value,
 * turned the just-blanked screen back on and armed the power-off countdown.
 *
 * The charger asks whether the cable is there through
 * ChargerCallback::ChargerIsOnline(), which this HAL provides: the filter
 * lives there (GtaxlChargerCallback), and only affects the charger's view.
 * HealthInfo stays the real one, and with Android running nothing changes,
 * because the callback only exists with --charger.
 */
class GtaxlHealth : public Health {
  public:
    using Health::Health;

    // The cable state as the charger must see it: an unplug counts only
    // after kUnpluggedDebounce without power.
    bool DebouncedChargerOnline(bool online);

    // When the debounce expires no event arrives to say the cable is still
    // missing: this HAL requests the wakeup itself, and on the next tick the
    // charger re-reads DebouncedChargerOnline().
    int OnPrepareToWait() override;

  protected:
    // BatteryMonitor reads the maximum current from the charger's
    // `current_max`, which sm5703-charger does not expose: its equivalent is
    // `input_current_limit`, which the driver picks from the port type (SDP
    // 500 mA, CDP 1.5 A, DCP the maximum). SystemUI needs it to say
    // «charging slowly» or «charging». The voltage stays 0: it is the power
    // adapter's, which nobody measures, and the framework assumes 5 V.
    void UpdateHealthInfo(HealthInfo* health_info) override;

  private:
    using Clock = std::chrono::steady_clock;

    bool seen_online_ = false;
    std::optional<Clock::time_point> unplugged_since_;
};

class GtaxlChargerCallback : public charger::ChargerCallback {
  public:
    explicit GtaxlChargerCallback(const std::shared_ptr<GtaxlHealth>& service)
        : ChargerCallback(service), service_(service) {}

    bool ChargerIsOnline() override;

  private:
    std::shared_ptr<GtaxlHealth> service_;
};

}  // namespace aidl::android::hardware::health
