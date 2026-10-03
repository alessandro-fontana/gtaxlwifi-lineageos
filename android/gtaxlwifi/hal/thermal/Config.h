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

#ifndef THERMAL_CONFIG_PARSER_H__
#define THERMAL_CONFIG_PARSER_H__

#include <aidl/android/hardware/thermal/BnThermal.h>

#include <map>
#include <string>

#include <json/reader.h>
#include <json/value.h>

namespace aidl {
namespace android {
namespace hardware {
namespace thermal {
namespace impl {
namespace linaro_generic {

class Config {
private:
	std::string toUpper(const std::string &str);

	bool read(std::string file);

	void initThreshold(TemperatureThreshold &tempThreshold);

	bool readHotColdThrottling(Json::Value &throttling, float *tempThreshold);

	bool readHotThrottling(Json::Value &throttling,
			       TemperatureThreshold &tempThreshold);

	bool readColdThrottling(Json::Value &throttling,
				TemperatureThreshold &tempThreshold);

	bool readVrThrottling(Json::Value &throttling,
			      TemperatureThreshold &tempThreshold);

	bool readThrottling(const std::string &name,
			    Json::Value &throttling,
			    TemperatureType type);

	bool readCoolingDevice(Json::Value &coolingDevice);

	bool readSensor(Json::Value &sensor);

	bool parseFile(std::string path, Json::Value &root);

public:
	std::vector<Temperature> m_temperature;

	std::vector<CoolingDevice> m_cooling_device;

	/*
	 * Each sensor can have associated a configuration for the
	 * threshold, the key is the name of the sensor.
	 */
	std::map<std::string, TemperatureThreshold> m_threshold;

	/*
	 * Contains the list of the skin temperature sensors.
	 */
	std::vector<std::string> m_skin_sensors;

	/*
	 * Contains the list of optional sensors.
	 */
	std::vector<std::string> m_optional;

	bool init(void);
};

}  // namespace linaro_generic
}  // namespace impl
}  // namespace thermal
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif
