#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <ctime>
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"

#define UNIX_EPOCH_TO_2000 946684800LL
#define NANOSECONDS_PER_SECOND 1000000000LL

const int32_t System_Time::s_daysBeforeMonth[13] = { 0, 31, 59, 90, 120, 151, 182, 212, 243, 273, 304, 334, 365 };

uint64_t System_Time::timeScaleCycles = 0xFFFFFFFFFFFFFFFF;

bool System_Time::s_usePerformanceCounter;

Blizzard::Lock::Atomic System_Time::s_tickLock;

int64_t System_Time::s_absBegin;

uint32_t System_Time::s_lastTickCount;

double System_Time::timeScaleMilliseconds;

Blizzard::Time::Timestamp System_Time::s_gmBegin;

uint64_t System_Time::s_tickCountHigh;

double System_Time::timeScaleNanoseconds;

double System_Time::timeScaleSeconds;

double System_Time::timeScaleMicroseconds;

static int64_t ClockNanoseconds(clockid_t clock) {
    timespec now;
    clock_gettime(clock, &now);

    return static_cast<int64_t>(now.tv_sec) * NANOSECONDS_PER_SECOND + now.tv_nsec;
}

static Blizzard::Time::Timestamp RealtimeTimestamp() {
    return ClockNanoseconds(CLOCK_REALTIME) - UNIX_EPOCH_TO_2000 * NANOSECONDS_PER_SECOND;
}

Blizzard::Time::Timestamp Blizzard::Time::FromFileTime(uint64_t fileTime) {
    if (fileTime <= 0x0077A5D6AA8BF851) {
        return static_cast<Timestamp>(0x8000000000000000);
    }

    if (fileTime <= 0x030701FFA04E87AE) {
        return static_cast<Timestamp>(fileTime * 100 + 0x5143382561530000);
    }

    return 0x7FFFFFFFFFFFFFFF;
}

double System_Time::MeasureDrift(int64_t frequency, LARGE_INTEGER* counter, DWORD* tickCount) {
    return 0.0;
}

uint64_t System_Time::GetTickCount64() {
    return static_cast<uint64_t>(ClockNanoseconds(CLOCK_MONOTONIC) / 1000000);
}

void Blizzard::Time::BreakTime(Timestamp time, TimeRec& rec) {
    int32_t nanosecond = static_cast<int32_t>(time % NANOSECONDS_PER_SECOND);
    int64_t seconds = time / NANOSECONDS_PER_SECOND;

    if (nanosecond < 0) {
        nanosecond += NANOSECONDS_PER_SECOND;
        seconds--;
    }

    time_t unixTime = static_cast<time_t>(seconds + UNIX_EPOCH_TO_2000);
    tm systemTime;
    gmtime_r(&unixTime, &systemTime);

    rec.day = systemTime.tm_mday;
    rec.hour = systemTime.tm_hour;
    rec.minute = systemTime.tm_min;
    rec.second = systemTime.tm_sec;
    rec.nanosecond = nanosecond;
    rec.weekday = systemTime.tm_wday;
    rec.year = systemTime.tm_year + 1900;
    rec.month = systemTime.tm_mon + 1;
    rec.yearday = systemTime.tm_yday;
}

Blizzard::Time::Timestamp Blizzard::Time::MakeTime(const TimeRec& rec) {
    tm systemTime = {};
    systemTime.tm_year = rec.year - 1900;
    systemTime.tm_mon = rec.month - 1;
    systemTime.tm_mday = rec.day;
    systemTime.tm_hour = rec.hour;
    systemTime.tm_min = rec.minute;
    systemTime.tm_sec = rec.second;

    int64_t seconds = static_cast<int64_t>(timegm(&systemTime)) - UNIX_EPOCH_TO_2000;

    return seconds * NANOSECONDS_PER_SECOND + rec.nanosecond;
}

bool System_Time::CalibrateFrequency(LARGE_INTEGER* frequency) {
    frequency->QuadPart = NANOSECONDS_PER_SECOND;

    return true;
}

bool System_Time::InitFrequency(LARGE_INTEGER* frequency) {
    return CalibrateFrequency(frequency);
}

bool System_Time::TimeInit() {
    LARGE_INTEGER frequency;
    s_usePerformanceCounter = InitFrequency(&frequency);
    timeScaleCycles = frequency.QuadPart;
    s_absBegin = ClockNanoseconds(CLOCK_MONOTONIC);
    s_gmBegin = RealtimeTimestamp();

    double scale = 1.0 / static_cast<double>(timeScaleCycles);
    timeScaleNanoseconds = 1000000000.0 * scale;
    timeScaleMicroseconds = 1000000.0 * scale;
    timeScaleMilliseconds = 1000.0 * scale;
    timeScaleSeconds = scale;

    return true;
}

Blizzard::Time::Timestamp Blizzard::Time::GetTimestamp() {
    if (System_Time::timeScaleCycles == 0xFFFFFFFFFFFFFFFF) {
        System_Time::TimeInit();
    }

    return System_Time::s_gmBegin + (ClockNanoseconds(CLOCK_MONOTONIC) - System_Time::s_absBegin);
}

int64_t Blizzard::Time::Milliseconds() {
    if (System_Time::timeScaleCycles == 0xFFFFFFFFFFFFFFFF) {
        System_Time::TimeInit();
    }

    return ClockNanoseconds(CLOCK_MONOTONIC) / 1000000;
}

#endif
