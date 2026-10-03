/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "SysfsFlag.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>

namespace gtaxlwifi::touch {

bool exists(const char* path) {
    std::string unused;
    return ::android::base::ReadFileToString(path, &unused);
}

::ndk::ScopedAStatus readFlag(const char* path, bool* out) {
    std::string contents;

    if (!::android::base::ReadFileToString(path, &contents)) {
        PLOG(ERROR) << "failed to read " << path;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(-EIO);
    }

    int value = 0;
    if (!::android::base::ParseInt(::android::base::Trim(contents), &value)) {
        LOG(ERROR) << "unparseable contents of " << path << ": '" << contents << "'";
        return ::ndk::ScopedAStatus::fromServiceSpecificError(-EINVAL);
    }

    *out = value != 0;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus writeFlag(const char* path, bool value) {
    if (!::android::base::WriteStringToFile(value ? "1" : "0", path)) {
        PLOG(ERROR) << "failed to write " << path;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(-EIO);
    }

    return ::ndk::ScopedAStatus::ok();
}

}  // namespace gtaxlwifi::touch
