/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.gnss-service.gtaxlwifi"

#include "Gnss.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <hardware/hardware.h>

using ::aidl::android::hardware::gnss::Gnss;

namespace {

// gps.default.so, i.e. the gpsd client (hardware/gps.h).
const GpsInterface* openLegacyGps() {
    const hw_module_t* module;
    int ret = hw_get_module(GPS_HARDWARE_MODULE_ID, &module);
    if (ret != 0) {
        LOG(ERROR) << "hw_get_module(" << GPS_HARDWARE_MODULE_ID << ") failed: " << ret;
        return nullptr;
    }

    hw_device_t* device;
    ret = module->methods->open(module, GPS_HARDWARE_MODULE_ID, &device);
    if (ret != 0) {
        LOG(ERROR) << "legacy gps open failed: " << ret;
        return nullptr;
    }

    auto* gpsDevice = reinterpret_cast<gps_device_t*>(device);
    return gpsDevice->get_gps_interface(gpsDevice);
}

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    const GpsInterface* gps = openLegacyGps();
    if (gps == nullptr) {
        // Without the legacy HAL there is nothing to serve; the service is
        // declared in the VINTF manifest, and dying here tells init and the
        // logs instead of answering into the void.
        LOG(FATAL) << "no legacy GPS interface";
    }

    auto gnss = ndk::SharedRefBase::make<Gnss>(gps);
    const std::string instance = std::string(Gnss::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(gnss->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        LOG(FATAL) << "failed to register " << instance << ": " << status;
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // never reached
}
