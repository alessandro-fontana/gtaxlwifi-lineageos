/*
 * Copyright (C) 2023 Linaro Ltd.
 * SPDX-FileCopyrightText: 2026 The LineageOS Project (modified: thermal
 * events delivered, binder polled from the same looper)
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
#define LOG_TAG "android.hardware.thermal-service.linaro-generic"

#include "Thermal.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hidl/HidlTransportSupport.h>

// libhwbinder:
using ::android::hardware::setupTransportPolling;
using ::android::hardware::handleTransportPoll;

// libutils:
using ::android::Looper;

using aidl::android::hardware::thermal::impl::linaro_generic::Thermal;

/*
 * TODO : more accurate exit values
 */
typedef enum {
	THERMAL_HAL_OK,
	THERMAL_HAL_INTERNAL_ERROR,
} thermal_hal_error_t;

static int shutdown(thermal_hal_error_t exit_code)
{
	LOG(ERROR) << "Thermal Service is shutting down";
	exit(exit_code);
}

static int transportCallback(int fd, __attribute__((unused)) int event,
			     __attribute__((unused)) void *data)
{
	handleTransportPoll(fd);
	return 1;
}

static int binderCallback(__attribute__((unused)) int fd,
			  __attribute__((unused)) int event,
			  __attribute__((unused)) void *data)
{
	ABinderProcess_handlePolledCommands();
	return 1;
}

static bool setupTransportCallback(Looper *looper)
{
	int fd;

	fd = setupTransportPolling();
	if (fd < 0)
		return false;

	if (!looper->addFd(fd, 0, Looper::EVENT_INPUT, transportCallback, NULL))
		return false;

	return true;
}

int main(int /* argc */, char** /* argv */)
{
	ABinderProcess_setThreadPoolMaxThreadCount(0);
	std::shared_ptr<Thermal> thermal;
	Looper *looper;

	LOG(DEBUG) << "Thermal HAL Service generic starting...";

	looper = new Looper(false);
	if (looper == nullptr) {
		LOG(ERROR) << "Failed to create looper object";
		return shutdown(THERMAL_HAL_INTERNAL_ERROR);
	}

	try {
		thermal = ndk::SharedRefBase::make<Thermal>(looper);
		if (thermal == nullptr) {
			LOG(ERROR) << "Error creating an instance of ThermalHAL.  Exiting...";
			return shutdown(THERMAL_HAL_INTERNAL_ERROR);
		}
	}

	catch(std::string error) {
		LOG(ERROR) << "Exception when creating Thermal object error: " << error;
		return shutdown(THERMAL_HAL_INTERNAL_ERROR);
	}

	// The constructors throw string literals.
	catch(const char *error) {
		LOG(ERROR) << "Exception when creating Thermal object error: " << error;
		return shutdown(THERMAL_HAL_INTERNAL_ERROR);
	}

	//
	// No threads for the transport layer. They are not needed for
	// our purpose.  Furthermore, adding threads will lead us to
	// the locking hell and an unexpected behavior with the
	// netlink thermal protocol.
	//
	if (!setupTransportCallback(looper)) {
		LOG(ERROR) << "Failed to setup transport callback";
		return shutdown(THERMAL_HAL_INTERNAL_ERROR);
	}

	const std::string instance = std::string() + Thermal::descriptor + "/default";
	binder_status_t status =
            AServiceManager_addService(thermal->asBinder().get(), instance.c_str());
	CHECK(status == STATUS_OK);

	// The binder commands go through the same looper as the thermal
	// events: ABinderProcess_joinThreadPool() never returns, and the
	// looper, with the netlink events socket in it, would never be polled.
	int binder_fd = -1;
	if (ABinderProcess_setupPolling(&binder_fd) != STATUS_OK || binder_fd < 0) {
		LOG(ERROR) << "Failed to set up binder polling";
		return shutdown(THERMAL_HAL_INTERNAL_ERROR);
	}

	if (!looper->addFd(binder_fd, 0, Looper::EVENT_INPUT, binderCallback, NULL)) {
		LOG(ERROR) << "Failed to add the binder file descriptor to the mainloop";
		return shutdown(THERMAL_HAL_INTERNAL_ERROR);
	}

	for (;;)
		looper->pollAll(-1);

	return EXIT_FAILURE;  // should not reach
}
