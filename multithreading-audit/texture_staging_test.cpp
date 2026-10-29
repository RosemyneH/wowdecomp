#include "Engine/Source/Services/TextureStaging.h"

#include <cstdint>

int main() {
    const int token = 1;
    const uint32_t reserved = 100;
    const uint32_t fileSize = 1000;

    if (TextureStagingUsed(reserved, nullptr, fileSize, 1) != reserved) {
        return 1;
    }

    if (TextureStagingUsed(0, nullptr, 64, 0) != 0) {
        return 2;
    }

    if (TextureStagingUsed(0, &token, 64, 0) != 64) {
        return 3;
    }

    if (TextureStagingUsed(64, &token, 64, 1) != 0) {
        return 4;
    }

    const uint32_t budget = 0x400000u;

    if (TextureStagingRemaining(budget - 10u, budget) != 10u) {
        return 5;
    }

    if (TextureStagingRemaining(budget, budget) != 0) {
        return 6;
    }

    if (TextureStagingRemaining(budget + 50u, budget) != 0) {
        return 7;
    }

    return 0;
}
