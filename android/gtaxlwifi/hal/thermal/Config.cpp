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
#include "Config.h"

#include <cmath>
#include <iostream>
#include <fstream>

#include <android-base/properties.h>
#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace thermal {
namespace impl {
namespace linaro_generic {

std::string Config::toUpper(const std::string &str)
{
	std::string upper_str(str);

	for (std::string::size_type i = 0; i < upper_str.length(); i++)
		upper_str[i] = std::toupper(str[i], std::locale());

	return upper_str;
}

bool Config::readHotColdThrottling(Json::Value &throttling,
				   float *tempThreshold)
{
	if (throttling.empty())
		return false;

	for (const auto& mn : throttling.getMemberNames()) {

		for (std::underlying_type<ThrottlingSeverity>::type ts = std::__to_underlying(ThrottlingSeverity::NONE); ts <= std::__to_underlying(ThrottlingSeverity::SHUTDOWN); ts++) {

			const std::string severity = toString(static_cast<ThrottlingSeverity>(ts));

			if (toUpper(severity) != toUpper(mn))
				continue;

			tempThreshold[(int)ts] = throttling[mn].asFloat();

			LOG(DEBUG) << "Throttling[\"" << severity << "\"] = " <<
				tempThreshold[(int)ts] << "°C";

		}
	}

	return true;
}

bool Config::readColdThrottling(Json::Value &throttling,
				TemperatureThreshold &tempThreshold)
{
	return readHotColdThrottling(throttling,
				     tempThreshold.coldThrottlingThresholds.data());
}

bool Config::readHotThrottling(Json::Value &throttling,
			       TemperatureThreshold &tempThreshold)
{
	return readHotColdThrottling(throttling,
				     tempThreshold.hotThrottlingThresholds.data());
}

void Config::initThreshold(TemperatureThreshold &tempThreshold)
{
	tempThreshold.hotThrottlingThresholds.resize(std::__to_underlying(ThrottlingSeverity::SHUTDOWN)+1);
	for (int i = 0; i < tempThreshold.hotThrottlingThresholds.size(); i++)
		tempThreshold.hotThrottlingThresholds[i] = NAN;

	tempThreshold.coldThrottlingThresholds.resize(std::__to_underlying(ThrottlingSeverity::SHUTDOWN)+1);
	for (int i = 0; i < tempThreshold.coldThrottlingThresholds.size(); i++)
		tempThreshold.coldThrottlingThresholds[i] = NAN;
}

bool Config::readThrottling(const std::string &name, Json::Value &throttling, TemperatureType type)
{
	TemperatureThreshold tempThreshold = {
		.name = name,
		.type = type,
	};

	initThreshold(tempThreshold);

	if (throttling.empty()) {
		LOG(DEBUG) << "No threshold specified for '" << name << "'";
		return true;
	}

	for (Json::Value::ArrayIndex i = 0; i < throttling.size(); ++i) {

		std::string type = throttling[i]["Type"].asString();

		LOG(DEBUG) << "Getting '" << type << "' throttling configuration";

		if (type == "Hot") {
			if (!readHotThrottling(throttling[i], tempThreshold)) {
				LOG(ERROR) << "Failed to read hot throttling entry";
				return false;
			}
		} else if (type == "Cold") {
			if (!readColdThrottling(throttling[i], tempThreshold)) {
				LOG(ERROR) << "Failed to read cold throttling entry";
				return false;
			}
		} else {
			LOG(ERROR) << "Invalid Throttling type: " << type;
			return false;
		}
	}

	this->m_threshold.insert(std::pair<std::string,
				 TemperatureThreshold>(name, tempThreshold));

	return true;
}

bool Config::readSensor(Json::Value &sensor)
{
	std::string name = sensor["Name"].asString();
	std::string type = sensor["Type"].asString();

	Temperature temperature;

	if (name.empty()) {
		LOG(ERROR) << "Missing sensor name section";
		return false;
	}

	temperature.name = name;
	temperature.type = TemperatureType::UNKNOWN;

	for (std::underlying_type<TemperatureType>::type tt = std::__to_underlying(TemperatureType::UNKNOWN); tt <= std::__to_underlying(TemperatureType::SOC); tt++) {
		const std::string tempType = toString(static_cast<TemperatureType>(tt));

		if (toUpper(tempType) == toUpper(type)) {
			temperature.type = static_cast<TemperatureType>(tt);
			break;
		}
	}

	m_temperature.push_back(temperature);

	if (sensor.get("Optional", false).asBool())
		m_optional.push_back(name);

	/*
	 * The skin temperature sensors are special ones and are
	 * stored in a second list for quick access for monitoring
	 */
	if (temperature.type == TemperatureType::SKIN)
		m_skin_sensors.push_back(name);

	LOG(DEBUG) << "Sensor: '" << name << "' / type: " << type;

	if (!readThrottling(name, sensor["Throttling"], temperature.type))
		return false;

	return true;
}

bool Config::readCoolingDevice(Json::Value &coolingDeviceNode)
{
	std::string name = coolingDeviceNode["Name"].asString();
	std::string type = coolingDeviceNode["Type"].asString();

	CoolingDevice coolingDevice;

	if (name.empty() || type.empty()) {
		LOG(ERROR) << "Missing Cooling device name/type";
		return false;
	}

	coolingDevice.name = name;

	for (std::underlying_type<CoolingType>::type ct = std::__to_underlying(CoolingType::FAN); ct <= std::__to_underlying(CoolingType::SPEAKER); ct++) {
		const std::string coolingType = toString(static_cast<CoolingType>(ct));

		if (toUpper(coolingType) == toUpper(type)) {
			coolingDevice.type = static_cast<CoolingType>(ct);
			break;
		}
	}

	m_cooling_device.push_back(coolingDevice);

	if (coolingDeviceNode.get("Optional", false).asBool())
		m_optional.push_back(name);

	return true;
}

bool Config::parseFile(std::string path, Json::Value &root)
{
	Json::CharReaderBuilder builder;
	std::string strerr;
	std::ifstream ifs;

	LOG(DEBUG) << "Reading configuration file: " << path;

	ifs.open(path);
	if (!ifs) {
		LOG(ERROR) << "Failed to open: " << path;
		return false;
	}

	if (!parseFromStream(builder, ifs, &root, &strerr))  {
		LOG(ERROR) << "Failed to parse JSON config: " << strerr;
		return false;
	}

	LOG(DEBUG) << "Configuration file parsed successfully";

	ifs.close();

	return true;
}

bool Config::read(std::string path)
{
	Json::Value root;
	Json::Value sensors;
	Json::Value coolingDevices;

	if (!parseFile(path, root))
		return false;

	sensors = root["Sensors"];

	for (Json::Value::ArrayIndex i = 0; i < sensors.size(); ++i) {

		if (!readSensor(sensors[i]))
			return false;
        }

	coolingDevices = root["CoolingDevices"];

	for (Json::Value::ArrayIndex i = 0; i < coolingDevices.size(); ++i) {

		if (!readCoolingDevice(coolingDevices[i]))
			return false;
        }

	return true;
}

bool Config::init(void)
{
	std::string property("vendor.thermal.config");
	std::string default_conf("thermal.json");
	std::string path = "/vendor/etc/" + ::android::base::GetProperty(property, default_conf);

	return read(path);
}

}  // namespace linaro_generic
}  // namespace impl
}  // namespace thermal
}  // namespace hardware
}  // namespace android
}  // namespace aidl
