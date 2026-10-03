/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "android.hardware.gnss-service.gtaxlwifi"

#include "Gnss.h"

#include <android-base/logging.h>
#include <utils/SystemClock.h>

#include <pthread.h>

namespace aidl::android::hardware::gnss {

namespace {

using GnssSvInfo = IGnssCallback::GnssSvInfo;

/*
 * The BCM4752 firmware counts GPS weeks on 10 bits with a base that
 * predates the rollover of 6 April 2019, 23:59:42 UTC (00:00:00 on the 7th
 * in GPS time). Since then, on the Wi-Fi variants - which have no network
 * time to inject - dates come out exactly 1024 weeks behind, i.e. starting
 * from August 1999. No real fix can have a time before the rollover: those
 * are moved forward.
 *
 * LineageOS 21 fixed the same thing in the framework's Location.getTime(),
 * with a threshold at 1 January 2019; here the fix lives in the HAL, which
 * is the only layer that knows which receiver the data comes from.
 */
constexpr int64_t kGpsWeekRolloverMs = 1554595182000LL;
constexpr int64_t k1024WeeksMs = 1024LL * 7 * 24 * 60 * 60 * 1000;

// The gps.h and IGnssCallback capabilities match bit for bit in the first
// eight. Only those actually served are declared: no MSB/MSA (there is no
// AGNSS, gps.xml has SuplEnable="false"), no geofence, measurements or
// navigation messages, whose extensions answer UNSUPPORTED.
constexpr uint32_t kCapabilitiesMask =
        GPS_CAPABILITY_SCHEDULING | GPS_CAPABILITY_SINGLE_SHOT | GPS_CAPABILITY_ON_DEMAND_TIME;

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus fromLegacy(int ret, const char* what) {
    if (ret == 0) return ndk::ScopedAStatus::ok();
    LOG(ERROR) << what << " failed: " << ret;
    return ndk::ScopedAStatus::fromServiceSpecificError(IGnss::ERROR_GENERIC);
}

// PRN numbering of the old GpsSvStatus, from before constellations.
void legacyPrnToSv(int prn, GnssSvInfo* sv) {
    if (prn >= 1 && prn <= 32) {
        sv->constellation = GnssConstellationType::GPS;
        sv->svid = prn;
    } else if (prn >= 33 && prn <= 64) {
        sv->constellation = GnssConstellationType::SBAS;
        sv->svid = prn + 87;
    } else if (prn >= 65 && prn <= 96) {
        sv->constellation = GnssConstellationType::GLONASS;
        sv->svid = prn - 64;
    } else if (prn >= 193 && prn <= 200) {
        sv->constellation = GnssConstellationType::QZSS;
        sv->svid = prn;
    } else if (prn >= 201 && prn <= 235) {
        sv->constellation = GnssConstellationType::BEIDOU;
        sv->svid = prn - 200;
    } else if (prn >= 301 && prn <= 336) {
        sv->constellation = GnssConstellationType::GALILEO;
        sv->svid = prn - 300;
    } else {
        sv->constellation = GnssConstellationType::UNKNOWN;
        sv->svid = prn;
    }
}

struct ThreadStart {
    void (*start)(void*);
    void* arg;
};

void* threadTrampoline(void* data) {
    ThreadStart ts = *static_cast<ThreadStart*>(data);
    delete static_cast<ThreadStart*>(data);
    ts.start(ts.arg);
    return nullptr;
}

}  // namespace

GpsCallbacks Gnss::sGpsCallbacks = {
        .size = sizeof(GpsCallbacks),
        .location_cb = Gnss::locationCb,
        .status_cb = Gnss::statusCb,
        .sv_status_cb = Gnss::svStatusCb,
        .nmea_cb = Gnss::nmeaCb,
        .set_capabilities_cb = Gnss::setCapabilitiesCb,
        .acquire_wakelock_cb = Gnss::acquireWakelockCb,
        .release_wakelock_cb = Gnss::releaseWakelockCb,
        .create_thread_cb = Gnss::createThreadCb,
        .request_utc_time_cb = Gnss::requestUtcTimeCb,
        .set_system_info_cb = Gnss::setSystemInfoCb,
        .gnss_sv_status_cb = Gnss::gnssSvStatusCb,
};

AGpsRilCallbacks Gnss::sAGpsRilCallbacks = {
        .request_setid = Gnss::agpsRilRequestSetIdCb,
        .request_refloc = Gnss::agpsRilRequestRefLocCb,
        .create_thread_cb = Gnss::createThreadCb,
};

AGpsCallbacks Gnss::sAGpsCallbacks = {
        .status_cb = Gnss::agpsStatusCb,
        .create_thread_cb = Gnss::createThreadCb,
};

GpsNiCallbacks Gnss::sGpsNiCallbacks = {
        .notify_cb = Gnss::niNotifyCb,
        .create_thread_cb = Gnss::createThreadCb,
};

GpsXtraCallbacks Gnss::sGpsXtraCallbacks = {
        .download_request_cb = Gnss::xtraDownloadRequestCb,
        .create_thread_cb = Gnss::createThreadCb,
};

const AGpsRilInterface* Gnss::sAGpsRil = nullptr;

std::mutex Gnss::sMutex;
std::shared_ptr<IGnssCallback> Gnss::sCallback;
int32_t Gnss::sCapabilities = 0;
int32_t Gnss::sYearOfHw = 0;
bool Gnss::sSvStatusEnabled = false;
bool Gnss::sNmeaEnabled = false;

Gnss::Gnss(const GpsInterface* gps) : mGps(gps) {}

std::shared_ptr<IGnssCallback> Gnss::callback() {
    std::lock_guard<std::mutex> lock(sMutex);
    return sCallback;
}

void Gnss::reportCapabilitiesAndInfo(const std::shared_ptr<IGnssCallback>& cb) {
    int32_t capabilities, year;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        capabilities = sCapabilities;
        year = sYearOfHw;
    }
    cb->gnssSetCapabilitiesCb(capabilities);
    if (year != 0) {
        cb->gnssSetSystemInfoCb({.yearOfHw = year, .name = "Broadcom BCM4752"});
    }
}

ndk::ScopedAStatus Gnss::setCallback(const std::shared_ptr<IGnssCallback>& callback) {
    if (callback == nullptr) {
        return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_ARGUMENT);
    }
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sCallback = callback;
    }

    std::lock_guard<std::mutex> lock(mInitMutex);
    if (!mInitialized) {
        // The capabilities and the year arrive inside init(), through the
        // callbacks, and find sCallback already set.
        int ret = mGps->init(&sGpsCallbacks);
        if (ret != 0) {
            LOG(ERROR) << "legacy init failed: " << ret;
            return ndk::ScopedAStatus::fromServiceSpecificError(ERROR_GENERIC);
        }
        mInitialized = true;
        initLegacyExtensions();
    } else {
        // A new system_server: the legacy HAL does not repeat the init
        // callbacks, so they are repeated from here.
        reportCapabilitiesAndInfo(callback);
    }
    return ndk::ScopedAStatus::ok();
}

/*
 * With the HIDL HALs it was the framework's requests (getExtensionAGnssRil
 * and friends) that initialised the legacy extensions. Here the framework
 * does not ask for them, because we do not declare them: but libgps calls
 * them anyway when gpsd asks it to. Measured: on the first session gpsd
 * asks for a reference location and libgps jumps into request_refloc of an
 * AGpsRilCallbacks that was never passed, SIGSEGV at 0x8 (tombstone_17).
 *
 * The AGPS, AGPS RIL and NI callbacks are inert on purpose: on a Wi-Fi-only
 * tablet there are no cells to give as reference, no SET-ID, and no network
 * to initiate a session. The gps-xtra one is not: the request for
 * assistance data goes to the framework through GnssPsds (§54.7).
 */
void Gnss::initLegacyExtensions() {
    auto ril = static_cast<const AGpsRilInterface*>(mGps->get_extension(AGPS_RIL_INTERFACE));
    if (ril != nullptr) {
        ril->init(&sAGpsRilCallbacks);
        std::lock_guard<std::mutex> lock(sMutex);
        sAGpsRil = ril;
    }
    auto agps = static_cast<const AGpsInterface*>(mGps->get_extension(AGPS_INTERFACE));
    if (agps != nullptr) agps->init(&sAGpsCallbacks);
    auto ni = static_cast<const GpsNiInterface*>(mGps->get_extension(GPS_NI_INTERFACE));
    if (ni != nullptr) ni->init(&sGpsNiCallbacks);
    auto xtra = static_cast<const GpsXtraInterface*>(mGps->get_extension(GPS_XTRA_INTERFACE));
    if (xtra != nullptr) xtra->init(&sGpsXtraCallbacks);

    LOG(INFO) << "legacy extensions: agps_ril " << (ril != nullptr) << ", agps "
              << (agps != nullptr) << ", gps-ni " << (ni != nullptr) << ", gps-xtra "
              << (xtra != nullptr);
}

ndk::ScopedAStatus Gnss::close() {
    std::lock_guard<std::mutex> lock(mInitMutex);
    if (mInitialized) {
        mGps->cleanup();
        mInitialized = false;
    }
    std::lock_guard<std::mutex> cbLock(sMutex);
    sCallback = nullptr;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::start() {
    return fromLegacy(mGps->start(), "start");
}

ndk::ScopedAStatus Gnss::stop() {
    return fromLegacy(mGps->stop(), "stop");
}

ndk::ScopedAStatus Gnss::injectTime(int64_t timeMs, int64_t timeReferenceMs,
                                    int32_t uncertaintyMs) {
    return fromLegacy(mGps->inject_time(timeMs, timeReferenceMs, uncertaintyMs), "inject_time");
}

ndk::ScopedAStatus Gnss::injectLocation(const GnssLocation& location) {
    return fromLegacy(mGps->inject_location(location.latitudeDegrees, location.longitudeDegrees,
                                            location.horizontalAccuracyMeters),
                      "inject_location");
}

ndk::ScopedAStatus Gnss::injectBestLocation(const GnssLocation& location) {
    // gps.h does not distinguish the "best" location: it is the same injection.
    return injectLocation(location);
}

ndk::ScopedAStatus Gnss::deleteAidingData(GnssAidingData aidingDataFlags) {
    // The GnssAidingData bits are those of GPS_DELETE_*.
    mGps->delete_aiding_data(static_cast<GpsAidingData>(aidingDataFlags));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::setPositionMode(const PositionModeOptions& options) {
    return fromLegacy(
            mGps->set_position_mode(static_cast<GpsPositionMode>(options.mode),
                                    static_cast<GpsPositionRecurrence>(options.recurrence),
                                    options.minIntervalMs, options.preferredAccuracyMeters,
                                    options.preferredTimeMs),
            "set_position_mode");
}

ndk::ScopedAStatus Gnss::startSvStatus() {
    std::lock_guard<std::mutex> lock(sMutex);
    sSvStatusEnabled = true;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::stopSvStatus() {
    std::lock_guard<std::mutex> lock(sMutex);
    sSvStatusEnabled = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::startNmea() {
    std::lock_guard<std::mutex> lock(sMutex);
    sNmeaEnabled = true;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Gnss::stopNmea() {
    std::lock_guard<std::mutex> lock(sMutex);
    sNmeaEnabled = false;
    return ndk::ScopedAStatus::ok();
}

// Extensions not served: see Gnss.h.
ndk::ScopedAStatus Gnss::getExtensionPsds(std::shared_ptr<IGnssPsds>* _aidl_return) {
    std::lock_guard<std::mutex> lock(mInitMutex);
    if (mPsds == nullptr) {
        auto xtra = static_cast<const GpsXtraInterface*>(mGps->get_extension(GPS_XTRA_INTERFACE));
        if (xtra == nullptr) return unsupported();
        mPsds = ndk::SharedRefBase::make<GnssPsds>(xtra);
    }
    *_aidl_return = mPsds;
    return ndk::ScopedAStatus::ok();
}
ndk::ScopedAStatus Gnss::getExtensionGnssConfiguration(std::shared_ptr<IGnssConfiguration>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssMeasurement(
        std::shared_ptr<IGnssMeasurementInterface>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssPowerIndication(
        std::shared_ptr<IGnssPowerIndication>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssBatching(std::shared_ptr<IGnssBatching>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssGeofence(std::shared_ptr<IGnssGeofence>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssNavigationMessage(
        std::shared_ptr<IGnssNavigationMessageInterface>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionAGnss(std::shared_ptr<IAGnss>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionAGnssRil(std::shared_ptr<IAGnssRil>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssDebug(std::shared_ptr<IGnssDebug>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssVisibilityControl(
        std::shared_ptr<visibility_control::IGnssVisibilityControl>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssAntennaInfo(std::shared_ptr<IGnssAntennaInfo>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionMeasurementCorrections(
        std::shared_ptr<measurement_corrections::IMeasurementCorrectionsInterface>*) {
    return unsupported();
}
ndk::ScopedAStatus Gnss::getExtensionGnssAssistanceInterface(
        std::shared_ptr<gnss_assistance::IGnssAssistanceInterface>*) {
    return unsupported();
}

// ----------------------------------------------------------------------------
// Legacy HAL callbacks, called from its threads.

void Gnss::locationCb(GpsLocation* location) {
    auto cb = callback();
    if (cb == nullptr || location == nullptr) return;

    int64_t timestampMs = location->timestamp;
    if (timestampMs > 0 && timestampMs < kGpsWeekRolloverMs) {
        timestampMs += k1024WeeksMs;
    }

    GnssLocation out = {
            // GPS_LOCATION_HAS_* and GnssLocation::HAS_* match bit for bit.
            .gnssLocationFlags = location->flags,
            .latitudeDegrees = location->latitude,
            .longitudeDegrees = location->longitude,
            .altitudeMeters = location->altitude,
            .speedMetersPerSec = location->speed,
            .bearingDegrees = location->bearing,
            .horizontalAccuracyMeters = location->accuracy,
            .timestampMillis = timestampMs,
            .elapsedRealtime =
                    {
                            .flags = ElapsedRealtime::HAS_TIMESTAMP_NS,
                            .timestampNs = ::android::elapsedRealtimeNano(),
                    },
    };
    cb->gnssLocationCb(out);
}

void Gnss::statusCb(GpsStatus* status) {
    auto cb = callback();
    if (cb == nullptr || status == nullptr) return;
    cb->gnssStatusCb(static_cast<IGnssCallback::GnssStatusValue>(status->status));
}

void Gnss::svStatusCb(GpsSvStatus* svStatus) {
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (!sSvStatusEnabled) return;
        cb = sCallback;
    }
    if (cb == nullptr || svStatus == nullptr) return;

    std::vector<GnssSvInfo> list;
    int n = std::min(svStatus->num_svs, static_cast<int>(GPS_MAX_SVS));
    for (int i = 0; i < n; i++) {
        const GpsSvInfo& in = svStatus->sv_list[i];
        GnssSvInfo sv = {
                .cN0Dbhz = in.snr,
                .elevationDegrees = in.elevation,
                .azimuthDegrees = in.azimuth,
                .svFlag = static_cast<int>(IGnssCallback::GnssSvFlags::NONE),
        };
        legacyPrnToSv(in.prn, &sv);
        // The old structure's masks only apply to GPS.
        if (sv.constellation == GnssConstellationType::GPS) {
            uint32_t bit = 1u << (in.prn - 1);
            if (svStatus->ephemeris_mask & bit)
                sv.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::HAS_EPHEMERIS_DATA);
            if (svStatus->almanac_mask & bit)
                sv.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::HAS_ALMANAC_DATA);
            if (svStatus->used_in_fix_mask & bit)
                sv.svFlag |= static_cast<int>(IGnssCallback::GnssSvFlags::USED_IN_FIX);
        }
        list.push_back(sv);
    }
    cb->gnssSvStatusCb(list);
}

void Gnss::gnssSvStatusCb(GnssSvStatus* svStatus) {
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (!sSvStatusEnabled) return;
        cb = sCallback;
    }
    if (cb == nullptr || svStatus == nullptr) return;

    std::vector<GnssSvInfo> list;
    int n = std::min(svStatus->num_svs, static_cast<int>(GNSS_MAX_SVS));
    for (int i = 0; i < n; i++) {
        const ::GnssSvInfo& in = svStatus->gnss_sv_list[i];
        list.push_back({
                .svid = in.svid,
                // GNSS_CONSTELLATION_* and GNSS_SV_FLAGS_* match AIDL.
                .constellation = static_cast<GnssConstellationType>(in.constellation),
                .cN0Dbhz = in.c_n0_dbhz,
                .elevationDegrees = in.elevation,
                .azimuthDegrees = in.azimuth,
                .svFlag = in.flags,
        });
    }
    cb->gnssSvStatusCb(list);
}

void Gnss::nmeaCb(GpsUtcTime timestamp, const char* nmea, int length) {
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (!sNmeaEnabled) return;
        cb = sCallback;
    }
    if (cb == nullptr || nmea == nullptr || length <= 0) return;
    cb->gnssNmeaCb(timestamp, std::string(nmea, length));
}

void Gnss::setCapabilitiesCb(uint32_t capabilities) {
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sCapabilities = static_cast<int32_t>(capabilities & kCapabilitiesMask);
        cb = sCallback;
    }
    LOG(INFO) << "legacy capabilities 0x" << std::hex << capabilities << ", declared 0x"
              << (capabilities & kCapabilitiesMask);
    if (cb != nullptr) cb->gnssSetCapabilitiesCb(capabilities & kCapabilitiesMask);
}

void Gnss::acquireWakelockCb() {
    auto cb = callback();
    if (cb != nullptr) cb->gnssAcquireWakelockCb();
}

void Gnss::releaseWakelockCb() {
    auto cb = callback();
    if (cb != nullptr) cb->gnssReleaseWakelockCb();
}

pthread_t Gnss::createThreadCb(const char* name, void (*start)(void*), void* arg) {
    pthread_t thread;
    auto* ts = new ThreadStart{start, arg};
    if (pthread_create(&thread, nullptr, threadTrampoline, ts) != 0) {
        LOG(ERROR) << "cannot create thread " << (name ? name : "?");
        delete ts;
        return 0;
    }
    if (name != nullptr) {
        // pthread_setname_np accepts at most 15 characters.
        pthread_setname_np(thread, std::string(name).substr(0, 15).c_str());
    }
    return thread;
}

void Gnss::agpsRilRequestSetIdCb(uint32_t flags) {
    const AGpsRilInterface* ril;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        ril = sAGpsRil;
    }
    // No SIM: answer "no SET-ID" instead of leaving gpsd waiting.
    LOG(INFO) << "AGPS RIL: set id requested (flags " << flags << "), none available";
    if (ril != nullptr) ril->set_set_id(AGPS_SETID_TYPE_NONE, "");
}

void Gnss::agpsRilRequestRefLocCb(uint32_t flags) {
    // No cell to give as reference location.
    LOG(INFO) << "AGPS RIL: reference location requested (flags " << flags << "), none available";
}

void Gnss::agpsStatusCb(AGpsStatus* status) {
    if (status != nullptr) LOG(INFO) << "AGPS status " << status->status;
}

void Gnss::niNotifyCb(GpsNiNotification* notification) {
    if (notification != nullptr) {
        LOG(INFO) << "network initiated request " << notification->notification_id << " ignored";
    }
}

void Gnss::xtraDownloadRequestCb() {
    GnssPsds::requestDownload();
}

// ----------------------------------------------------------------------------
// GnssPsds

std::mutex GnssPsds::sMutex;
std::shared_ptr<IGnssPsdsCallback> GnssPsds::sCallback;

GnssPsds::GnssPsds(const GpsXtraInterface* xtra) : mXtra(xtra) {}

ndk::ScopedAStatus GnssPsds::setCallback(const std::shared_ptr<IGnssPsdsCallback>& callback) {
    std::lock_guard<std::mutex> lock(sMutex);
    sCallback = callback;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus GnssPsds::injectPsdsData(PsdsType psdsType,
                                            const std::vector<uint8_t>& psdsData) {
    // gps-xtra knows only one data type, the long-term one.
    if (psdsType != PsdsType::LONG_TERM) {
        return ndk::ScopedAStatus::fromServiceSpecificError(IGnss::ERROR_INVALID_ARGUMENT);
    }
    LOG(INFO) << "injecting " << psdsData.size() << " bytes of long term PSDS data";
    int ret = mXtra->inject_xtra_data(
            reinterpret_cast<char*>(const_cast<uint8_t*>(psdsData.data())),
            static_cast<int>(psdsData.size()));
    return fromLegacy(ret, "inject_xtra_data");
}

void GnssPsds::requestDownload() {
    std::shared_ptr<IGnssPsdsCallback> cb;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        cb = sCallback;
    }
    LOG(INFO) << "PSDS download requested" << (cb == nullptr ? ", no callback yet" : "");
    if (cb != nullptr) cb->downloadRequestCb(PsdsType::LONG_TERM);
}

void Gnss::requestUtcTimeCb() {
    auto cb = callback();
    if (cb != nullptr) cb->gnssRequestTimeCb();
}

void Gnss::setSystemInfoCb(const ::GnssSystemInfo* info) {
    if (info == nullptr) return;
    std::shared_ptr<IGnssCallback> cb;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        sYearOfHw = info->year_of_hw;
        cb = sCallback;
    }
    if (cb != nullptr) {
        cb->gnssSetSystemInfoCb({.yearOfHw = info->year_of_hw, .name = "Broadcom BCM4752"});
    }
}

}  // namespace aidl::android::hardware::gnss
