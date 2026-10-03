/*
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef ANDROID_HARDWARE_THERMAL_LINARO_GENERIC_THERMAL_H
#define ANDROID_HARDWARE_THERMAL_LINARO_GENERIC_THERMAL_H

#include <aidl/android/hardware/thermal/BnThermal.h>

#include <utils/Looper.h>

#include "Config.h"
#include "LibThermal.h"

namespace aidl {
namespace android {
namespace hardware {
namespace thermal {
namespace impl {
namespace linaro_generic {

using ::android::Looper;
using ::android::LooperCallback;

struct ThermalCallbackSetting {
    ThermalCallbackSetting(std::shared_ptr<IThermalChangedCallback> callback, TemperatureType type)
	    : callback(std::move(callback)), type(type) {}
	std::shared_ptr<IThermalChangedCallback> callback;
	TemperatureType type;
};

class Thermal : public LibThermal, public BnThermal {
   public:
	// Methods from android.hardware.thermal V1 follow.
	ndk::ScopedAStatus getTemperatures(std::vector<Temperature>* out_temperatures) override;
	ndk::ScopedAStatus getTemperaturesWithType(TemperatureType in_type, std::vector<Temperature>* out_temperatures) override;

	ndk::ScopedAStatus getCoolingDevices(std::vector<CoolingDevice>* out_devices) override;
	ndk::ScopedAStatus getCoolingDevicesWithType(CoolingType in_type, std::vector<CoolingDevice>* out_devices) override;

	ndk::ScopedAStatus getTemperatureThresholds(std::vector<TemperatureThreshold>* out_temperatureThresholds) override;
	ndk::ScopedAStatus getTemperatureThresholdsWithType(TemperatureType in_type, std::vector<TemperatureThreshold>* out_temperatureThresholds) override;

	ndk::ScopedAStatus registerThermalChangedCallback(const std::shared_ptr<IThermalChangedCallback>& in_callback) override;
	ndk::ScopedAStatus registerThermalChangedCallbackWithType(const std::shared_ptr<IThermalChangedCallback>& in_callback, TemperatureType in_type) override;
	ndk::ScopedAStatus unregisterThermalChangedCallback(const std::shared_ptr<IThermalChangedCallback>& in_callback) override;

	int handleThermalEvents(void);

	Thermal(Looper *);

private:
	static int thermalZoneDelete(int, void *);
	static int thermalZoneCreate(const char *, int, void *);
	static int thermalZoneEnable(int tz_id, void *arg);
	static int thermalZoneDisable(int tz_id, void *arg);
	static int tripHigh(int tz_id, int trip_id, int temp, void *arg);
	static int tripLow(int tz_id, int trip_id, int temp, void *arg);
	static int tripAdd(int tz_id, int trip_id, int type,
			   int temp, int hyst, void *arg);

	static int tripChange(int tz_id, int trip_id, int type,
			      int temp, int hyst, void *arg);
	static int tripDelete(int tz_id, int trip_id, void *arg);
	static int cdevAdd(const char *name, int cdev_id, int max_state, void *arg);
	static int cdevDelete(int cdev_id, void *arg);
	static int cdevUpdate(int cdev_id, int state, void *arg);
	static int govChange(int tz_id, const char *name, void *arg);

	void thermalChangedCallback(Temperature &temperature);

	ThrottlingSeverity throttlingSeverity(const std::string &name, float temperature);

	int tripCrossed(int tz_id, int trip_id, int temp, Thermal *thermal, bool up);

	Config m_config;

	std::mutex m_callback_mutex;
	std::vector<ThermalCallbackSetting> m_callbacks;
};

}  // namespace linaro_generic
}  // namespace impl
}  // namespace thermal
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif  // ANDROID_HARDWARE_THERMAL_LINARO_GENERIC_THERMAL_H
