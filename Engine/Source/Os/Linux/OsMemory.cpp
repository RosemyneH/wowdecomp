#include <cstdint>
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>
#include "Engine/Source/Os/OsMemory.h"

#if defined(WOWDECOMP_ARCH_64)

static const uintptr_t s_nearRange = 0x60000000;
static const uintptr_t s_headerSize = 16;

static void* MapAt(uintptr_t address, uintptr_t bytes) {
    void* ptr = mmap(reinterpret_cast<void*>(address), bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (ptr == MAP_FAILED) {
        return nullptr;
    }

    if (reinterpret_cast<uintptr_t>(ptr) != address) {
        munmap(ptr, bytes);
        return nullptr;
    }

    *static_cast<uintptr_t*>(ptr) = bytes;

    return static_cast<uint8_t*>(ptr) + s_headerSize;
}

void* OsAllocNear(const void* anchor, size_t size) {
    uintptr_t granularity = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    uintptr_t bytes = (size + s_headerSize + granularity - 1) & ~(granularity - 1);
    uintptr_t center = reinterpret_cast<uintptr_t>(anchor);
    uintptr_t minimum = center > granularity + s_nearRange ? center - s_nearRange : granularity;
    uintptr_t maximum = center + s_nearRange;

    FILE* maps = fopen("/proc/self/maps", "r");

    if (!maps) {
        return nullptr;
    }

    uintptr_t bestAddress = 0;
    uintptr_t bestDistance = UINTPTR_MAX;
    uintptr_t gapStart = minimum;
    unsigned long start;
    unsigned long end;
    char line[512];

    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%lx-%lx", &start, &end) != 2) {
            continue;
        }

        uintptr_t gapEnd = start < maximum ? start : maximum;

        if (gapEnd > gapStart && gapEnd - gapStart >= bytes) {
            uintptr_t candidate;

            if (gapEnd <= center) {
                candidate = (gapEnd - bytes) & ~(granularity - 1);
            } else if (gapStart >= center) {
                candidate = (gapStart + granularity - 1) & ~(granularity - 1);
            } else {
                candidate = (center + granularity - 1) & ~(granularity - 1);
            }

            if (candidate >= gapStart && candidate + bytes <= gapEnd) {
                uintptr_t distance = candidate > center ? candidate - center : center - candidate;

                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestAddress = candidate;
                }
            }
        }

        if (end > gapStart) {
            gapStart = end;
        }

        if (gapStart >= maximum) {
            break;
        }
    }

    fclose(maps);

    if (gapStart < maximum && maximum - gapStart >= bytes) {
        uintptr_t candidate = (gapStart + granularity - 1) & ~(granularity - 1);
        uintptr_t distance = candidate > center ? candidate - center : center - candidate;

        if (candidate + bytes <= maximum && distance < bestDistance) {
            bestAddress = candidate;
        }
    }

    return bestAddress ? MapAt(bestAddress, bytes) : nullptr;
}

void OsFreeNear(void* ptr) {
    if (ptr) {
        uint8_t* base = static_cast<uint8_t*>(ptr) - s_headerSize;
        munmap(base, *reinterpret_cast<uintptr_t*>(base));
    }
}

#endif
