#include <atomic>
#include <cstdint>
#include <ctime>
#include <mach/mach_time.h>
#include <new>
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

static OsTimeManager* s_OsTimeMgr;

OsTimeManager::TimingMethod OsTimeManager::Calibrate() {
    mach_timebase_info_data_t timebase;

    if (mach_timebase_info(&timebase) || !timebase.numer) {
        this->m_timingTestError = 1;
        return SystemMethod1;
    }

    this->m_performanceFrequency = static_cast<int64_t>(1000000000ull * timebase.denom / timebase.numer);
    this->m_timingTestError = 0;

    return SystemMethod2;
}

// OFFSET: 0x0086AD50
uint32_t OsTimeGetTestError() {
    return s_OsTimeMgr->m_timingTestError;
}

// OFFSET: 0x0086AD60
OsTimeManager::TimingMethod OsTimeGetTimingMethod() {
    return s_OsTimeMgr->m_timingMethod;
}

const char* GetTimingMethodName(OsTimeManager::TimingMethod method) {
    switch (method) {
        case OsTimeManager::NotSet:
            return "[Not Set]";

        case OsTimeManager::BestAvailable:
            return "[Best Available]";

        case OsTimeManager::SystemMethod1:
            return "CLOCK_MONOTONIC_RAW_APPROX";

        case OsTimeManager::SystemMethod2:
            return "mach_absolute_time";

        default:
            return "[Unknown]";
    }
}

uint64_t OsGetAsyncTimeClocks() {
    if (s_OsTimeMgr->m_timingMethod == OsTimeManager::SystemMethod2) {
        return mach_absolute_time();
    }

    static std::atomic<uint64_t> lastGood;
    uint64_t nanos = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW_APPROX);
    if (!nanos) {
        return lastGood.load(std::memory_order_relaxed);
    }

    uint64_t millis = nanos / 1000000;
    lastGood.store(millis, std::memory_order_relaxed);
    return millis;
}

// OFFSET: 0x0086ADC0
uint64_t OsTimeManager::Snapshot() {
    return static_cast<int64_t>(static_cast<double>(OsGetAsyncTimeClocks()) * this->m_scaleToMs + this->m_timeBegin);
}

// OFFSET: 0x0086AE20
uint32_t OsGetAsyncTimeMs() {
    return static_cast<uint32_t>(s_OsTimeMgr->Snapshot());
}

// OFFSET: 0x0086AE70
int64_t OsGetAsyncClocksPerSecond() {
    switch (s_OsTimeMgr->m_timingMethod) {
        case OsTimeManager::SystemMethod1:
            break;

        case OsTimeManager::SystemMethod2:
            return s_OsTimeMgr->m_performanceFrequency;

        default:
            break;
    }

    return 1000;
}

// OFFSET: 0x0086AEA0
OsTimeManager::OsTimeManager(OsTimeManager::TimingMethod method) {
    this->m_timingMethod = NotSet;

    OsTimeManager::TimingMethod calibrated = this->Calibrate();
    if (method != BestAvailable && calibrated != method) {
        if (method == SystemMethod2 && calibrated == SystemMethod1) {
            this->m_timingTestError = 5;
        }

        calibrated = method;
    }

    this->m_timingMethod = calibrated;

    int64_t clocksPerSecond;
    switch (calibrated) {
        case SystemMethod1:
            clocksPerSecond = 1000;
            break;

        case SystemMethod2:
            clocksPerSecond = this->m_performanceFrequency;
            break;

        default:
            clocksPerSecond = 1000;
            break;
    }

    this->m_scaleToMs = 1000.0 / static_cast<double>(clocksPerSecond);
    this->m_timeBegin = 0.0;
}

// OFFSET: 0x0086AF20
void OsTimeManagerCreate(OsTimeManager::TimingMethod method) {
    s_OsTimeMgr = new (SMemAlloc(sizeof(OsTimeManager), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY)) OsTimeManager(method);
}

// OFFSET: 0x0086AF60
void OsTimeManagerDestroy() {
    if (s_OsTimeMgr) {
        delete s_OsTimeMgr;
        s_OsTimeMgr = nullptr;
    }
}

void OsSleep(uint32_t milliseconds) {
    timespec duration;
    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000;

    while (nanosleep(&duration, &duration)) {
    }
}
