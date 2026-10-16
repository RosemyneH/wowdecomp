#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstring>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Hardware/Hardware.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

Blizzard::Hardware::ProcessorFeatures System_Hardware::s_processorFeatures;

bool System_Hardware::s_initialized;

Blizzard::Lock::Atomic System_Hardware::s_initCount;

bool System_Hardware::ReadCPUID(CPUIDInfo* info) {
    memset(info, 0, sizeof(*info));

#if defined(__x86_64__) || defined(__i386__)
    uint32_t regs[4];

    if (!__get_cpuid_count(0, 0, &regs[0], &regs[1], &regs[2], &regs[3]) || !regs[0]) {
        return false;
    }

    uint32_t maxLeaf = regs[0];
    info->m_maxLeaf = regs[0];
    info->m_vendor[0] = regs[1];
    info->m_vendor[1] = regs[3];
    info->m_vendor[2] = regs[2];

    __cpuid_count(0x80000000, 0, regs[0], regs[1], regs[2], regs[3]);

    if (regs[0] <= 0x80000000) {
        return false;
    }

    uint32_t maxExtLeaf = regs[0];
    info->m_maxExtLeaf = regs[0];

    if (maxLeaf >= 4) {
        __cpuid_count(4, 0, regs[0], regs[1], regs[2], regs[3]);
        info->m_leaf4Eax = regs[0];
    }

    __cpuid_count(1, 0, regs[0], regs[1], regs[2], regs[3]);
    info->m_leaf1Edx = regs[3];
    info->m_leaf1Ebx = regs[1];

    if (maxExtLeaf >= 0x80000008) {
        __cpuid_count(0x80000008, 0, regs[0], regs[1], regs[2], regs[3]);
        info->m_ext8Ecx = regs[2];
    }

    if (maxExtLeaf >= 0x80000004) {
        __cpuid_count(0x80000004, 0, regs[0], regs[1], regs[2], regs[3]);
        info->m_ext4Eax = regs[0];
    }

    __cpuid_count(0x80000001, 0, regs[0], regs[1], regs[2], regs[3]);
    info->m_ext1Edx = regs[3];
    info->m_ext1Ecx = regs[2];

    return true;
#else
    return false;
#endif
}

void System_Hardware::InitProcessorFeatures() {
    uint32_t features = 0;
    long processors = sysconf(_SC_NPROCESSORS_ONLN);

    s_processorFeatures.m_processorCount = processors > 0 ? static_cast<uint32_t>(processors) : 1;
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

        const char* vendor = reinterpret_cast<const char*>(info.m_vendor);

        if (!strncmp(vendor, "GenuineIntel", 12)) {
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
        } else if (!strncmp(vendor, "AuthenticAMD", 12)) {
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
        }
    }

    s_processorFeatures.m_features = features;
}

uint32_t Blizzard::Hardware::GetProcessorFeatures() {
    if (!System_Hardware::s_initialized) {
        if (System_Hardware::s_initCount.Increment() == 1) {
            System_Hardware::InitProcessorFeatures();
            System_Hardware::s_initialized = true;
            return System_Hardware::s_processorFeatures.m_features;
        }

        do {
            usleep(1000);
        } while (!System_Hardware::s_initialized);
    }

    return System_Hardware::s_processorFeatures.m_features;
}

#endif
