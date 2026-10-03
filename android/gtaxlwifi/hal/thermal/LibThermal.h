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
#include <string>

#include <thermal.h>

namespace aidl {
namespace android {
namespace hardware {
namespace thermal {
namespace impl {
namespace linaro_generic {

class LibThermalCallbacks {
public:
	/*
	 * Callbacks called when a thermal zone event occurs
	 */
	int (*thermalZoneCreate)(const char *, int, void *);
	int (*thermalZoneDelete) (int, void *);
	int (*thermalZoneEnable)(int, void *);
	int (*thermalZoneDisable)(int, void *);

	/*
	 * Callbacks called when a trip event occurs
	 */
	int (*tripHigh)(int, int, int, void *);
	int (*tripLow)(int, int, int, void *);
	int (*tripAdd)(int, int, int, int, int, void *);
	int (*tripChange)(int, int, int, int, int, void *);
	int (*tripDelete)(int, int, void *);

	/*
	 * Callbacks called when a cooling device event occurs
	 */
	int (*cdevAdd)(const char *, int, int, void *);
	int (*cdevDelete)(int, void *);
	int (*cdevUpdate)(int, int, void *);

	/*
	 * Callback called when a governor event occurs
	 */
	int (*govChange)(int, const char *, void *);

	/*
	 * Callback called when there is a temperature sampling
	 */
	int (*thermalZoneTemp)(int, int, void *);
};

class LibThermal {

private:
	struct thermal_zone *getThermalZone(int id);
	struct thermal_zone *getThermalZone(std::string name);
	struct thermal_cdev *getThermalCdev(int id);
	struct thermal_cdev *getThermalCdev(std::string name);

protected:
	struct thermal_ops	 m_ops;
	struct thermal_handler	*m_th;
	struct thermal_zone	*m_tz;
	struct thermal_cdev	*m_cdev;

	LibThermal();
public:
	int getThermalZonetemp(const std::string name);
	int getThermalZonetemp(struct thermal_zone *tz);
	int getThermalZonetemp(int id);

	std::string getThermalZoneName(int id);
	std::string getThermalCdevName(int id);

	bool thermalZoneExists(const std::string name);
	bool thermalCdevExists(const std::string name);
};

}  // namespace linaro_generic
}  // namespace impl
}  // namespace thermal
}  // namespace hardware
}  // namespace android
}  // namespace aidl
