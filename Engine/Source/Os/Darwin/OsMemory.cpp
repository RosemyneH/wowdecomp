#include <cstdint>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include "Engine/Source/Os/OsMemory.h"

#if defined(WOWDECOMP_ARCH_64)

static const uintptr_t s_nearRange = 0x60000000;
static const uintptr_t s_headerSize = 16;

static void* MapAt(uintptr_t address, uintptr_t bytes) {
    mach_vm_address_t ptr = address;

    if (mach_vm_allocate(mach_task_self(), &ptr, bytes, VM_FLAGS_FIXED) != KERN_SUCCESS) {
        return nullptr;
    }

    *reinterpret_cast<uintptr_t*>(ptr) = bytes;

    return reinterpret_cast<uint8_t*>(ptr) + s_headerSize;
}

static void ConsiderGap(uintptr_t gapStart, uintptr_t gapEnd, uintptr_t center, uintptr_t bytes, uintptr_t granularity, uintptr_t& bestAddress, uintptr_t& bestDistance) {
    if (gapEnd <= gapStart || gapEnd - gapStart < bytes) {
        return;
    }

    uintptr_t candidate;

    if (gapEnd <= center) {
        candidate = (gapEnd - bytes) & ~(granularity - 1);
    } else if (gapStart >= center) {
        candidate = (gapStart + granularity - 1) & ~(granularity - 1);
    } else {
        candidate = (center + granularity - 1) & ~(granularity - 1);
    }

    if (candidate < gapStart || candidate + bytes > gapEnd) {
        return;
    }

    uintptr_t distance = candidate > center ? candidate - center : center - candidate;

    if (distance < bestDistance) {
        bestDistance = distance;
        bestAddress = candidate;
    }
}

void* OsAllocNear(const void* anchor, size_t size) {
    uintptr_t granularity = vm_page_size;
    uintptr_t bytes = (size + s_headerSize + granularity - 1) & ~(granularity - 1);
    uintptr_t center = reinterpret_cast<uintptr_t>(anchor);
    uintptr_t minimum = center > granularity + s_nearRange ? center - s_nearRange : granularity;
    uintptr_t maximum = center + s_nearRange;

    uintptr_t bestAddress = 0;
    uintptr_t bestDistance = UINTPTR_MAX;
    uintptr_t gapStart = minimum;
    mach_vm_address_t address = minimum;

    while (gapStart < maximum) {
        mach_vm_size_t regionSize = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object;

        if (mach_vm_region(mach_task_self(), &address, &regionSize, VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count, &object) != KERN_SUCCESS) {
            break;
        }

        uintptr_t start = static_cast<uintptr_t>(address);
        uintptr_t end = static_cast<uintptr_t>(address + regionSize);

        ConsiderGap(gapStart, start < maximum ? start : maximum, center, bytes, granularity, bestAddress, bestDistance);

        if (end > gapStart) {
            gapStart = end;
        }

        address = end;
    }

    if (gapStart < maximum) {
        ConsiderGap(gapStart, maximum, center, bytes, granularity, bestAddress, bestDistance);
    }

    return bestAddress ? MapAt(bestAddress, bytes) : nullptr;
}

void OsFreeNear(void* ptr) {
    if (ptr) {
        uint8_t* base = static_cast<uint8_t*>(ptr) - s_headerSize;
        mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(base), *reinterpret_cast<uintptr_t*>(base));
    }
}

#endif
