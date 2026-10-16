#if defined(WOWDECOMP_ARCH_64)

#include <cstdint>
#include <cstring>
#include <windows.h>
#if defined(_M_X64)
#include <intrin.h>
#endif
#include "Engine/Source/BlizzardCore/Source/System/Hardware/Hardware.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

// STATIC: 0x00B33FDC
Blizzard::Hardware::ProcessorFeatures System_Hardware::s_processorFeatures;

// STATIC: 0x00B33FEC
bool System_Hardware::s_initialized;

// STATIC: 0x00B33FF0
Blizzard::Lock::Atomic System_Hardware::s_initCount;

#if defined(_M_ARM64)

bool System_Hardware::ReadCPUID(CPUIDInfo* info) {
    memset(info, 0, sizeof(*info));
    return false;
}

void System_Hardware::InitProcessorFeatures() {
    SYSTEM_INFO systemInfo = {};
    GetSystemInfo(&systemInfo);

    s_processorFeatures.m_processorCount = systemInfo.dwNumberOfProcessors ? systemInfo.dwNumberOfProcessors : 1;
    s_processorFeatures.m_coreCount = 1;

    uint32_t features = 0;

    if (s_processorFeatures.m_processorCount > 1) {
        features = 0x40;
        s_processorFeatures.m_coreCount = s_processorFeatures.m_processorCount;
    }

    s_processorFeatures.m_features = features;
}

#elif defined(_M_X64)

// OFFSET: 0x0042E120
bool System_Hardware::ReadCPUID(CPUIDInfo* info) {
    memset(info, 0, sizeof(*info));
    bool result = false;
    uint32_t maxLeaf = 0;
    uint32_t maxExtLeaf = 0;

    uintptr_t flags = __readeflags();
    __writeeflags(flags ^ 0x200000);

    if (__readeflags() == flags) {
        return result;
    }

    int32_t regs[4];
    __cpuidex(regs, 0, 0);

    if (!regs[0]) {
        return result;
    }

    maxLeaf = regs[0];
    info->m_maxLeaf = regs[0];
    info->m_vendor[0] = regs[1];
    info->m_vendor[1] = regs[3];
    info->m_vendor[2] = regs[2];

    __cpuidex(regs, 0x80000000, 0);

    if (static_cast<uint32_t>(regs[0]) <= 0x80000000) {
        return result;
    }

    maxExtLeaf = regs[0];
    info->m_maxExtLeaf = regs[0];
    result = true;

    if (maxLeaf >= 4) {
        __cpuidex(regs, 4, 0);
        info->m_leaf4Eax = regs[0];
    }

    __cpuidex(regs, 1, 0);
    info->m_leaf1Edx = regs[3];
    info->m_leaf1Ebx = regs[1];

    if (maxExtLeaf >= 0x80000008) {
        __cpuidex(regs, 0x80000008, 0);
        info->m_ext8Ecx = regs[2];
    }

    if (maxExtLeaf >= 0x80000004) {
        __cpuidex(regs, 0x80000004, 0);
        info->m_ext4Eax = regs[0];
    }

    __cpuidex(regs, 0x80000001, 0);
    info->m_ext1Edx = regs[3];
    info->m_ext1Ecx = regs[2];

    return result;
}

// OFFSET: 0x00432D90
void System_Hardware::InitProcessorFeatures() {
    SYSTEM_INFO systemInfo = {};
    uint32_t features = 0;

    GetSystemInfo(&systemInfo);
    s_processorFeatures.m_processorCount = systemInfo.dwNumberOfProcessors;

    if (!systemInfo.dwNumberOfProcessors) {
        s_processorFeatures.m_processorCount = 1;
    }

    s_processorFeatures.m_coreCount = 1;

    CPUIDInfo info;

    if (ReadCPUID(&info)) {
        if (info.m_leaf1Edx & 0x10) {
            features = 0x1;
        }

        if (info.m_leaf1Edx & 0x800000) {
            features |= 0x2;
        }

        if (info.m_leaf1Edx & 0x2000000) {
            features |= 0x4;
        }

        if (info.m_ext1Edx & 0x80000000) {
            features |= 0x8;
        }

        const char* vendors = "GenuineIntel" "AuthenticAMD" "CyrixInstead" "CentaurHalls";
        const char* vendor = reinterpret_cast<const char*>(info.m_vendor);

        if (!strncmp(vendor, vendors, 12)) {
            s_processorFeatures.m_vendor = 1;

            if (info.m_leaf1Edx & 0x4000000) {
                features |= 0x10;
            }

            if (info.m_maxLeaf >= 4) {
                uint8_t cores = static_cast<uint8_t>(info.m_leaf4Eax >> 26);

                if (cores) {
                    s_processorFeatures.m_features = features | 0x40;
                    s_processorFeatures.m_coreCount = cores + 1;
                    return;
                }
            }
        } else if (!strncmp(vendor, vendors + 12, 12)) {
            s_processorFeatures.m_vendor = 2;

            if (info.m_leaf1Edx & 0x4000000) {
                features |= 0x10;
            }

            uint8_t cores = static_cast<uint8_t>(info.m_ext8Ecx);

            if (cores) {
                s_processorFeatures.m_features = features | 0x40;
                s_processorFeatures.m_coreCount = cores + 1;
                return;
            }
        } else if (!strncmp(vendor, vendors + 24, 12)) {
        } else if (!strncmp(vendor, vendors + 36, 12)) {
        }
    }

    s_processorFeatures.m_features = features;
}

#endif

// OFFSET: 0x00435BB0
uint32_t Blizzard::Hardware::GetProcessorFeatures() {
    if (!System_Hardware::s_initialized) {
        if (System_Hardware::s_initCount.Increment() == 1) {
            System_Hardware::InitProcessorFeatures();
            System_Hardware::s_initialized = true;
            return System_Hardware::s_processorFeatures.m_features;
        }

        do {
            Sleep(1);
        } while (!System_Hardware::s_initialized);
    }

    return System_Hardware::s_processorFeatures.m_features;
}

#endif
