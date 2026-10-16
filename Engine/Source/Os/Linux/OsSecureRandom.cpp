#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <sys/random.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/Packages/Util/SHA1.h"
#include "Engine/Source/Os/OsSecureRandom.h"
#include "Engine/Source/Storm/H/storm.h"

struct OsSecureRandomEntropy {
    timespec m_monotonic;
    timespec m_realtime;
    struct sysinfo m_memoryStatus;
    struct statvfs m_diskStatus;
    pid_t m_processId;
    pid_t m_threadId;
    uint32_t m_counter;
    void* m_heapPointer;
};

static std::atomic<uint32_t> s_counter;

// OFFSET: 0x0086D4C0
void IOsSecureRandom(uint8_t* buffer, uint32_t size) {
    OsSecureRandomEntropy entropy;
    memset(&entropy, 0, sizeof(entropy));

    clock_gettime(CLOCK_MONOTONIC, &entropy.m_monotonic);
    clock_gettime(CLOCK_REALTIME, &entropy.m_realtime);
    sysinfo(&entropy.m_memoryStatus);
    statvfs(".", &entropy.m_diskStatus);

    entropy.m_processId = getpid();
    entropy.m_threadId = gettid();

    entropy.m_heapPointer = SMemAlloc(1, __FILE__, __LINE__, 0);

    if (entropy.m_heapPointer) {
        SMemFree(entropy.m_heapPointer, __FILE__, __LINE__, 0);
    }

    entropy.m_counter = ++s_counter;

    SHA1_CONTEXT context;
    uint8_t digest[20];

    SHA1_Init(&context);
    SHA1_Update(&context, buffer, size);
    SHA1_Update(&context, reinterpret_cast<const uint8_t*>(&entropy), sizeof(entropy));
    SHA1_Final(digest, &context);

    while (true) {
        uint32_t count = size > 10 ? 10 : size;
        memcpy(buffer, digest, count);
        size -= count;
        buffer += count;

        if (!size) {
            break;
        }

        SHA1_Init(&context);
        SHA1_Update(&context, digest, sizeof(digest));
        SHA1_Final(digest, &context);
    }
}

// OFFSET: 0x0086D640
void OsSecureRandom(uint8_t* buffer, uint32_t size) {
    uint32_t filled = 0;

    while (filled < size) {
        ssize_t result = getrandom(buffer + filled, size - filled, 0);

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result <= 0) {
            IOsSecureRandom(buffer, size);
            return;
        }

        filled += static_cast<uint32_t>(result);
    }
}
