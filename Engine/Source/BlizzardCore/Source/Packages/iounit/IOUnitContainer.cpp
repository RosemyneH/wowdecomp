#include <cstdarg>
#include <cstdint>
#include "Engine/Source/BlizzardCore/Source/Packages/iounit/IOUnitContainer.h"
#include "Engine/Source/Storm/H/storm.h"

// VERIFY: 8efaa12c manual
// OFFSET: 0x0042B9C0
IOUnitContainer::IOUnitContainer(uint32_t size, ...) {
    uint32_t total = (size + 3) & ~3u;

    va_list args;
    va_start(args, size);

    for (uint32_t next = va_arg(args, uint32_t); next; next = va_arg(args, uint32_t)) {
        total = (next + total + 3) & ~3u;
    }

    va_end(args);

    this->m_next = static_cast<uint8_t*>(SMemAlloc(total, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    this->m_owner = true;
}

// VERIFY: b37dd59c manual
// OFFSET: 0x0042BA10
void* IOUnitContainer::Alloc(IOUnitContainer* container, uint32_t size, bool* ownsMemory) {
    if (!container) {
        *ownsMemory = true;
        return SMemAlloc(size, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
    }

    uint8_t* memory = container->m_next;

    if (!memory) {
        *ownsMemory = false;
        return nullptr;
    }

    container->m_next = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(memory) + size + 3) & ~static_cast<uintptr_t>(3));
    *ownsMemory = container->m_owner;
    container->m_owner = false;

    return memory;
}

// INLINED
void IOUnitContainer::Free(void* memory, bool ownsMemory) {
    if (ownsMemory) {
        SMemFree(memory, __FILE__, __LINE__, 0);
    }
}
