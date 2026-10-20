#include <cstdint>
#if defined(__linux__)
#include <sys/mman.h>
#endif
#include "Engine/Source/Gx/CGxTex.h"

int32_t GxPageMapped(uintptr_t bits) {
#if defined(__linux__)
    uintptr_t page = bits & ~uintptr_t{0xfff};
    static thread_local uintptr_t seen[128];
    static thread_local uint32_t count = 0;

    for (uint32_t i = 0; i < count; ++i) {
        if (seen[i] == page) {
            return 1;
        }
    }

    unsigned char resident = 0;

    if (mincore(reinterpret_cast<void*>(page), 4096, &resident) != 0) {
        return 0;
    }

    if (count < 128) {
        seen[count++] = page;
    } else {
        seen[(page >> 12) & 127] = page;
    }

    return 1;
#else
    (void)bits;
    return 1;
#endif
}

// VERIFY: 39ee55f7 manual
// OFFSET: 0x006852C0
CGxTex::CGxTex(EGxTexTarget target, uint32_t width, uint32_t height, uint32_t depth, EGxTexFormat format, EGxTexFormat dataFormat, CGxTexFlags flags, void* userArg, GxTexUserFunc userFunc, const char* name) {
    this->m_updateRect.minY = 0;
    this->m_updateRect.minX = 0;
    this->m_updateRect.maxY = height;
    this->m_updateRect.maxX = width;
    this->m_updatePlaneMin = -1;
    this->m_updatePlaneMax = -1;
    this->m_target = target;
    this->m_width = width;
    this->m_height = height;
    this->m_dataFormat = dataFormat;
    this->m_format = format;
    this->m_apiSpecificData = nullptr;
    this->m_apiSpecificData2 = nullptr;
    this->m_depth = depth;
    this->m_userFunc = userFunc;
    this->m_needsUpdate = 1;
    this->m_needsFlagUpdate = 1;
    this->m_needsCreation = 1;
    this->m_userArg = userArg;
    this->m_char5D = 1;
    this->m_flags = flags;
}

// VERIFY: bd9c61cb manual
// OFFSET: 0x00685DC0
CGxTex::~CGxTex() {
}
