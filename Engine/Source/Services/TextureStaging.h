#pragma once

#include <cstdint>

inline uint32_t TextureStagingUsed(uint32_t used, const void* buffer, uint32_t size, int32_t release) {
    if (!buffer) {
        return used;
    }

    return release ? used - size : used + size;
}

inline uint32_t TextureStagingRemaining(uint32_t used, uint32_t budget) {
    return used < budget ? budget - used : 0;
}
