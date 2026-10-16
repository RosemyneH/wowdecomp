#include <cstdint>
#include "Engine/Source/Os/Processor.h"

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

// VERIFY: e905bbd2 auto
// OFFSET: 0x0086BB80
uint32_t OsGetProcessorFeatures() {
    int32_t vendor;
    return OsGetProcessorFeaturesEx(vendor);
}

namespace {

int32_t DetectAvx2() {
#if defined(_MSC_VER)
    int regs[4] = {};
    __cpuid(regs, 0);

    if (static_cast<uint32_t>(regs[0]) < 7) {
        return 0;
    }

    __cpuid(regs, 1);

    // ʕ •ᴥ•ʔ✿ Leaf 1 ECX: OSXSAVE, AVX, and FMA ✿ ʕ •ᴥ•ʔ
    if ((static_cast<uint32_t>(regs[2]) & 0x18001000u) != 0x18001000u) {
        return 0;
    }

    if ((_xgetbv(0) & 0x6) != 0x6) {
        return 0;
    }

    __cpuidex(regs, 7, 0);
    return (static_cast<uint32_t>(regs[1]) & 0x20u) ? 1 : 0;
#elif defined(__x86_64__) || defined(__i386__)
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;

    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        return 0;
    }

    // ʕ •ᴥ•ʔ✿ Leaf 1 ECX: OSXSAVE, AVX, and FMA ✿ ʕ •ᴥ•ʔ
    if ((ecx & 0x18001000u) != 0x18001000u) {
        return 0;
    }

    unsigned int xcr0 = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0) : "c"(0) : "edx");

    if ((xcr0 & 0x6u) != 0x6u) {
        return 0;
    }

    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        return 0;
    }

    return (ebx & 0x20u) ? 1 : 0;
#else
    return 0;
#endif
}

}

int32_t OsHasAvx2() {
    static const int32_t cached = DetectAvx2();
    return cached;
}
