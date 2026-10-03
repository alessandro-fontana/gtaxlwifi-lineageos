/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SysfsFlag.h"
#include "Touch.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using ::aidl::vendor::lineage::touch::KeyDisabler;
using ::aidl::vendor::lineage::touch::KeySwapper;

namespace {

/*
 * ALWAYS registers, even if the sysfs node is missing.
 * LineageHardwareManager looks up the service with waitForDeclaredService():
 * declared in the VINTF manifest but never registered means the caller hangs
 * — i.e. Settings freezing when the Buttons section is opened. Better a
 * service that answers with an error.
 */
template <typename T>
std::shared_ptr<T> registerOrDie(const char* node) {
    if (!::gtaxlwifi::touch::exists(node)) {
        LOG(ERROR) << node << " is missing: " << T::descriptor
                   << " will answer, but every call will fail";
    }

    auto service = ::ndk::SharedRefBase::make<T>();
    const std::string name = std::string(T::descriptor) + "/default";

    binder_status_t status =
            AServiceManager_addService(service->asBinder().get(), name.c_str());
    if (status != STATUS_OK) {
        LOG(FATAL) << "failed to register " << name << ": " << status;
    }

    return service;
}

}  // namespace

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(1);

    auto swapper = registerOrDie<KeySwapper>(::gtaxlwifi::touch::kKeySwapPath);
    auto disabler = registerOrDie<KeyDisabler>(::gtaxlwifi::touch::kKeyDisablePath);

    ABinderProcess_joinThreadPool();

    return EXIT_FAILURE;  // never reached
}
