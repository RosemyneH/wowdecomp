#include "Engine/Source/Gx/CGxDevice/EmergencyMem.h"

#include <cstdlib>
#include <cstring>

void* STORMAPI SMemAlloc(size_t size, const char*, int32_t, uint32_t) {
    return std::malloc(size);
}

int32_t STORMAPI SMemFree(void* ptr, const char*, int32_t, uint32_t) {
    std::free(ptr);
    return 1;
}

void* STORMAPI SMemReAlloc(void* ptr, size_t size, const char*, int32_t, uint32_t) {
    return std::realloc(ptr, size);
}

int main() {
    EmergencyMem staging;
    auto* first = static_cast<unsigned char*>(staging.Lock(32));
    if (!first || !staging.m_lock) return 1;
    std::memset(first, 0x11, 32);
    auto* second = static_cast<unsigned char*>(staging.Lock(32));
    if (first != second) return 2;
    std::memset(second, 0x22, 32);
    for (unsigned i = 0; i < 32; ++i) {
        if (first[i] != 0x22) return 3;
    }
    staging.Unlock();
    if (staging.m_lock) return 4;

    unsigned char uploaded[2][32];
    for (unsigned i = 0; i < 2; ++i) {
        auto* mapped = static_cast<unsigned char*>(staging.Lock(32));
        if (!mapped || !staging.m_lock) return 5;
        std::memset(mapped, static_cast<int>(0x33 + i), 32);
        std::memcpy(uploaded[i], mapped, 32);
        staging.Unlock();
        if (staging.m_lock) return 6;
    }
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned char byte : uploaded[i]) {
            if (byte != 0x33 + i) return 7;
        }
    }
    return 0;
}
