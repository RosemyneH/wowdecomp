#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>
#if !(defined(__aarch64__) || defined(__arm64__))
#include <cpuid.h>
#endif
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/Processor.h"
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

extern char** environ;

static int32_t s_sleepInBackground = 1;

static const char* s_osTypeNames[] = { "Unknown", "Win95", "Win95OSR2", "Win98", "Win98SE", "WinME", "WinNT4", "Win2000", "WinXP", "Win2003", "Win9X_Other", "WinNT_Other", "MacOS9", "MacOSX", "Linux", "WinVista" };

static const uint8_t s_hexDigitValues[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static int32_t s_haveProcessorFeatures;

static int32_t s_processorVendor;

static uint32_t s_processorFeatures;

static int32_t s_processorSockets;

static int32_t s_processorCores;

static int32_t s_processorCount;

static uintptr_t s_textStart;

static uintptr_t s_textEnd;

static int32_t s_cpuLogEnabled;

static uint32_t s_backgroundSleepMs;

static uint64_t s_cpuTicksPerSecond;

static int32_t s_gotPowerProfFrequency;

static int32_t s_checkedPowerProfInfo;

static const char s_vendorIds[] = "GenuineIntel" "AuthenticAMD" "CyrixInstead" "CentaurHauls";

template <typename T>
static int32_t ReadSysctl(const char* name, T& value) {
    size_t size = sizeof(T);
    return !sysctlbyname(name, &value, &size, nullptr, 0) && size == sizeof(T);
}

#if defined(__aarch64__) || defined(__arm64__)

// ʕ •ᴥ•ʔ✿ Apple Silicon has no CPUID, so only the vendor and brand strings are filled from sysctl ✿ ʕ •ᴥ•ʔ
int32_t IOsGetProcessorFeatures(CpuIdInfo* info) {
    memset(info, 0, sizeof(CpuIdInfo));
    memcpy(info->m_vendor, "AppleSilicon", sizeof(info->m_vendor));

    size_t size = sizeof(info->m_brand) - 1;
    sysctlbyname("machdep.cpu.brand_string", info->m_brand, &size, nullptr, 0);

    return 1;
}

#else

// OFFSET: 0x0086AF90
int32_t IOsGetProcessorFeatures(CpuIdInfo* info) {
    memset(info, 0, sizeof(CpuIdInfo));

    uint32_t regs[4];
    uint32_t maxStandard = __get_cpuid_max(0, nullptr);

    if (!maxStandard) {
        return 0;
    }

    __cpuid(0, regs[0], regs[1], regs[2], regs[3]);
    info->m_maxStandard = maxStandard;
    memcpy(&info->m_vendor[0], &regs[1], 4);
    memcpy(&info->m_vendor[4], &regs[3], 4);
    memcpy(&info->m_vendor[8], &regs[2], 4);

    if (maxStandard >= 4) {
        __cpuid_count(4, 0, regs[0], regs[1], regs[2], regs[3]);
        info->m_standard4a = regs[0];
    }

    __cpuid(1, regs[0], regs[1], regs[2], regs[3]);
    info->m_standard1d = regs[3];
    info->m_standard1b = regs[1];

    uint32_t maxExtended = __get_cpuid_max(0x80000000, nullptr);

    if (maxExtended <= 0x80000000) {
        return 1;
    }

    info->m_maxExtended = maxExtended;

    if (maxExtended >= 0x80000008) {
        __cpuid(0x80000008, regs[0], regs[1], regs[2], regs[3]);
        info->m_extended8c = regs[2];
    }

    for (uint32_t leaf = 0; leaf < 3; leaf++) {
        if (maxExtended >= 0x80000002 + leaf) {
            __cpuid(0x80000002 + leaf, regs[0], regs[1], regs[2], regs[3]);
            memcpy(&info->m_brand[leaf * 16], regs, 16);
        }
    }

    __cpuid(0x80000001, regs[0], regs[1], regs[2], regs[3]);
    info->m_extended1d = regs[3];
    info->m_extended1c = regs[2];

    return 2;
}

#endif

// OFFSET: 0x0086B0C0
void OsSystemEnableCpuLog() {
    s_cpuLogEnabled = 1;
}

// OFFSET: 0x0086B0D0
void IOsSystemCpuLog(const CpuIdInfo* info) {
    if (!s_cpuLogEnabled) {
        return;
    }

    OsCreateDirectory("Logs", 0);

    HSLOG log;
    SLogCreate("Logs/cpu.log", 0, &log);

    if (s_processorVendor == 4) {
        SLogWrite(log, "UNABLE TO IDENTIFY CPU");
        SLogClose(log);
        return;
    }

    SLogWrite(log, "vendor: %d", s_processorVendor);
    SLogWrite(log, "features: %08X", s_processorFeatures & 0x7FFFFFFF);
    SLogWrite(log, "sockets: %d", s_processorSockets);
    SLogWrite(log, "cores: %d", s_processorCores);
    SLogWrite(log, "processors: %d", s_processorCount);

    char vendor[16];
    strncpy(vendor, info->m_vendor, 12);
    vendor[12] = '\0';
    SLogWrite(log, "vendor id string= %s", vendor);

    SLogWrite(log, "standard (%d): 1b=%08X 1d=%08x 4a=%08X", info->m_maxStandard, info->m_standard1b, info->m_standard1d, info->m_standard4a);
    SLogWrite(log, "extended (%d): 1c=%08X 1d=%08x 8c=%08X", info->m_maxExtended & 0x7FFFFFFF, info->m_extended1c, info->m_extended1d, info->m_extended8c);

    const char* brand = info->m_brand;

    while (*brand == ' ') {
        brand++;
    }

    SLogWrite(log, "processor brand string= %s", brand);
    SLogClose(log);
}

uint32_t OsGetNumberOfProcessors() {
    int32_t count;

    return ReadSysctl("hw.logicalcpu", count) && count > 0 ? static_cast<uint32_t>(count) : 1;
}

// OFFSET: 0x0086B290
int32_t OsSleepInBackground() {
    return s_sleepInBackground;
}

// OFFSET: 0x0086B2A0
void OsSetSleepInBackground(int32_t sleep) {
    s_sleepInBackground = sleep;
}

// OFFSET: 0x0086B2B0
uint32_t OsGetBackgroundSleepMs() {
    return s_backgroundSleepMs;
}

// OFFSET: 0x0086B2C0
void OsSetBackgroundSleepMs(uint32_t milliseconds) {
    s_backgroundSleepMs = milliseconds;
}

OsType OsGetVersion() {
    return OsType_MacOSX;
}

// OFFSET: 0x0086B430
void OsGetVersionString(char* buffer, int32_t size) {
    const char* name = s_osTypeNames[OsGetVersion()];

    if (!name) {
        name = s_osTypeNames[0];
    }

    SStrCopy(buffer, name, size);
}

// OFFSET: 0x0086B460
uint32_t OsGetProcessorId() {
    return 0x783836;
}

// ʕ •ᴥ•ʔ✿ Reports "Win" so 3.3.5a realm servers accept the logon challenge ✿ ʕ •ᴥ•ʔ
// OFFSET: 0x0086B470
uint32_t OsGetOsId() {
    return 0x57696E;
}

int32_t OsGetComputerName(char* buffer, uint32_t* size) {
    char name[256];

    if (gethostname(name, sizeof(name))) {
        return 0;
    }

    name[sizeof(name) - 1] = '\0';
    uint32_t length = static_cast<uint32_t>(strlen(name));

    if (length >= *size) {
        *size = length + 1;
        return 0;
    }

    memcpy(buffer, name, length + 1);
    *size = length;

    return 1;
}

int32_t OsGetUserName(char* buffer, uint32_t* size) {
    passwd* entry = getpwuid(geteuid());
    const char* name = entry ? entry->pw_name : getenv("USER");

    if (!name) {
        return 0;
    }

    uint32_t length = static_cast<uint32_t>(strlen(name)) + 1;

    if (length > *size) {
        *size = length;
        return 0;
    }

    memcpy(buffer, name, length);
    *size = length;

    return 1;
}

uint64_t OsGetPhysicalMemory() {
    uint64_t memory;

    return ReadSysctl("hw.memsize", memory) ? memory : 0;
}

void* CreateNamedEventA(const char* name, int32_t manualReset) {
    return nullptr;
}

void OsFindTextSectionBounds() {
    unsigned long size = 0;
    uint8_t* text = getsectiondata(&_mh_execute_header, "__TEXT", "__text", &size);

    s_textStart = reinterpret_cast<uintptr_t>(text);
    s_textEnd = s_textStart + size;

    if (s_textEnd <= s_textStart) {
        SErrDisplayAppFatal("Unable to find .text section");
    }
}

// OFFSET: 0x0086B5A0
void OsValidateFunctionPointer(const void* ptr) {
    if (!s_textStart || !s_textEnd) {
        OsFindTextSectionBounds();
    }

    if (reinterpret_cast<uintptr_t>(ptr) < s_textStart || reinterpret_cast<uintptr_t>(ptr) >= s_textEnd) {
        char buffer[64];
        SStrPrintf(buffer, sizeof(buffer), "Invalid function pointer: %p", ptr);
        SErrDisplayAppFatal(buffer);
    }
}

// OFFSET: 0x0086B600
int32_t IOsParseProcessorFrequency(const char* brand) {
    if (!brand) {
        return 0;
    }

    const char* p = brand + strlen(brand) - 1;

    if (p <= brand || !*p || *p != 'z') {
        return 0;
    }

    p--;

    if (p <= brand || !*p || *p != 'H') {
        return 0;
    }

    p--;

    if (p <= brand) {
        return 0;
    }

    double scale;

    if (*p == 'G') {
        scale = 1000000000.0;
    } else if (*p == 'M') {
        scale = 1000000.0;
    } else if (*p == 'T') {
        scale = 1000000000000.0;
    } else {
        return 0;
    }

    char number[48];
    memset(number, ' ', sizeof(number));
    number[47] = '\0';

    p--;
    char* dst = &number[46];

    while (p > brand && *p && ((*p >= '0' && *p <= '9') || *p == '.')) {
        *dst = *p;
        p--;
        dst--;
    }

    double value = atof(number);

    if (value == 0.0) {
        return 0;
    }

    s_cpuTicksPerSecond = static_cast<int64_t>(value * scale);

    return 1;
}

// ʕ •ᴥ•ʔ✿ hw.cpufrequency_max only exists on Intel Macs; Apple Silicon falls back to the timebase ✿ ʕ •ᴥ•ʔ
int32_t IOsGetPowerProfFrequency() {
    uint64_t hertz;

    if (!ReadSysctl("hw.cpufrequency_max", hertz) || !hertz) {
        return 0;
    }

    s_cpuTicksPerSecond = hertz;

    return 1;
}

int32_t OsIsRemoteSession() {
    return getenv("SSH_CONNECTION") != nullptr;
}

int32_t OsLaunchURL(const char* url) {
    if (!url || !*url) {
        return 0;
    }

    if (SStrLen(url) >= 1024) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    char file[1024];
    char* dst = file;
    const char* src = url + 1;

    for (char c = *url; c; c = *src++, dst++) {
        if (c == '%') {
            char high = *src++;

            if (!high) {
                break;
            }

            char low = *src++;

            if (!low) {
                break;
            }

            *dst = (s_hexDigitValues[high & 0x7F] << 4) | s_hexDigitValues[low & 0x7F];
        } else {
            *dst = c;
        }
    }

    *dst = '\0';

    char shell[] = "sh";
    char flag[] = "-c";
    char script[] = "open \"$1\" >/dev/null 2>&1 &";
    char* argv[] = { shell, flag, script, shell, file, nullptr };
    pid_t pid;

    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, environ)) {
        return 0;
    }

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);

    return waited == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// OFFSET: 0x0086B9A0
uint32_t OsGetProcessorFeaturesEx(int32_t& vendor) {
    if (s_haveProcessorFeatures) {
        vendor = s_processorVendor;
        return s_processorFeatures;
    }

    s_haveProcessorFeatures = 1;

    uint32_t features = 0;

    s_processorCount = OsGetNumberOfProcessors();
    s_processorCores = 1;
    s_processorSockets = 1;

    CpuIdInfo info;

    if (IOsGetProcessorFeatures(&info)) {
        if (info.m_standard1d & 0x10) {
            features = 1;
        }

        if (info.m_standard1d & 0x800000) {
            features |= 2;
        }

        if (info.m_standard1d & 0x2000000) {
            features |= 4;
        }

        if (info.m_extended1d & 0x80000000) {
            features |= 8;
        }

        if (info.m_standard1d & 0x4000000) {
            features |= 0x10;
        }

        if (!strncmp(info.m_vendor, s_vendorIds, 12)) {
            s_processorVendor = 1;

            if (info.m_maxStandard >= 4 && (info.m_standard4a >> 26)) {
                features |= 0x40;
                s_processorCores = static_cast<uint8_t>(info.m_standard4a >> 26) + 1;
            }
        } else if (!strncmp(info.m_vendor, s_vendorIds + 12, 12)) {
            s_processorVendor = 2;

            uint8_t cores = static_cast<uint8_t>(info.m_extended8c);

            if (cores) {
                features |= 0x40;
                s_processorCores = cores + 1;
            }
        } else {
            int32_t cores;

            if (ReadSysctl("hw.physicalcpu", cores) && cores > 1) {
                features |= 0x40;
                s_processorCores = cores;
            }
        }
    } else {
        s_processorVendor = 4;
    }

    s_processorFeatures = features | 0x80000000;

    IOsSystemCpuLog(&info);

    if (!IOsParseProcessorFrequency(info.m_brand)) {
        if (!s_checkedPowerProfInfo) {
            s_gotPowerProfFrequency = IOsGetPowerProfFrequency();
            s_checkedPowerProfInfo = 1;
        }

        if (!s_gotPowerProfFrequency) {
            s_cpuTicksPerSecond = OsGetAsyncClocksPerSecond();
        }
    }

    vendor = s_processorVendor;
    return s_processorFeatures;
}

// OFFSET: 0x0086BBA0
uint64_t OsGetProcessorTicksPerSecond() {
    if (!s_cpuTicksPerSecond) {
        int32_t vendor;
        OsGetProcessorFeaturesEx(vendor);
    }

    return s_cpuTicksPerSecond;
}
