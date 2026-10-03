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
#include <android-base/logging.h>

#include "LibThermal.h"

namespace aidl {
namespace android {
namespace hardware {
namespace thermal {
namespace impl {
namespace linaro_generic {

struct thermal_cdev *LibThermal::getThermalCdev(int id)
{
	return thermal_cdev_find_by_id(this->m_cdev, id);
}

struct thermal_cdev *LibThermal::getThermalCdev(std::string name)
{
	return thermal_cdev_find_by_name(this->m_cdev, name.c_str());
}

struct thermal_zone *LibThermal::getThermalZone(int id)
{
	return thermal_zone_find_by_id(this->m_tz, id);
}

struct thermal_zone *LibThermal::getThermalZone(std::string name)
{
	return thermal_zone_find_by_name(this->m_tz, name.c_str());
}

int LibThermal::getThermalZonetemp(struct thermal_zone *tz)
{
	if (thermal_cmd_get_temp(this->m_th, tz)) {
		LOG(ERROR) << "Failed to read '" << tz->name << "' temperature";
		return INT_MAX;
	}

	return tz->temp;
}

int LibThermal::getThermalZonetemp(int id)
{
	struct thermal_zone *tz;

	tz = getThermalZone(id);
	if (!tz) {
		LOG(ERROR) << "Thermal zone <id=" << id << "> not found";
		return INT_MAX;
	}

	return getThermalZonetemp(tz);
}

int LibThermal::getThermalZonetemp(const std::string name)
{
	struct thermal_zone *tz;

	tz = getThermalZone(name);
	if (!tz) {
		LOG(ERROR) << "Thermal zone <" << name << "> not found";
		return INT_MAX;
	}

	LOG(DEBUG) << "Getting temperature for thermal zone "
		   << name << " id=" << tz->id;

	return getThermalZonetemp(tz);
}

bool LibThermal::thermalZoneExists(const std::string name)
{
	return getThermalZone(name) != NULL;
}

std::string LibThermal::getThermalZoneName(int id)
{
	struct thermal_zone *tz;

	tz = getThermalZone(id);
	if (!tz)
		return std::string("");

	return std::string(tz->name);
}

std::string LibThermal::getThermalCdevName(int id)
{
	struct thermal_cdev *cdev;

	cdev = getThermalCdev(id);
	if (!cdev)
		return std::string("");

	return std::string(cdev->name);
}

bool LibThermal::thermalCdevExists(const std::string name)
{
	return getThermalCdev(name) != NULL;
}

LibThermal::LibThermal(void)
{
	LOG(DEBUG) << "Initializing the thermal library";

	m_th = thermal_init(&m_ops);
	if (!m_th)
		throw("Failed to initialize the thermal library");

	m_tz = thermal_zone_discover(m_th);
	if (!m_tz)
		throw("Failed to discover the thermal zones");

	if (thermal_cmd_get_cdev(m_th, &m_cdev))
		throw("Failed to discover the cooling devices");
}

}  // namespace linaro_generic
}  // namespace impl
}  // namespace thermal
}  // namespace hardware
}  // namespace android
}  // namespace aidl
