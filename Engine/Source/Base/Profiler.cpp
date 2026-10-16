#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include "Engine/Source/Os/TimeManager.h"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

constexpr uint32_t kFrames = 120;
constexpr uint32_t kNameBytes = 32;
constexpr uint32_t kMagic = 0x46525057u;
constexpr uint32_t kVersion = 1;

struct ZoneAccum {
    uint64_t selfTicks;
    uint64_t totalTicks;
    uint32_t count;
    uint32_t pad;
};

struct FrameRecord {
    uint64_t frameTicks;
    uint32_t serial;
    uint32_t pad;
    ZoneAccum zones[Profiler::Count];
};

struct SharedProfile {
    uint32_t magic;
    uint32_t version;
    uint32_t zoneCount;
    uint32_t frameCapacity;
    uint32_t nameBytes;
    uint32_t enabled;
    uint64_t clocksPerSecond;
    std::atomic<uint32_t> serial;
    uint32_t pad;
    char names[Profiler::Count][kNameBytes];
    FrameRecord frames[kFrames];
};

struct OpenScope {
    uint32_t id;
    uint64_t start;
    uint64_t child;
};

const char* const kNames[Profiler::Count] = {
    "World.Render",
    "Scene.Render",
    "Terrain.Flush",
    "Vis.Flush",
    "M2.Draw",
    "Gx.Draw",
    "Job.Join",
};

static_assert(sizeof(ZoneAccum) == 24, "profile zone");
static_assert(sizeof(FrameRecord) == 16 + 24 * Profiler::Count, "profile frame");
static_assert(offsetof(SharedProfile, frames) == 264, "profile frames");

SharedProfile* g_shm = nullptr;
ZoneAccum g_zones[Profiler::Count];
FrameRecord g_history[kFrames];
OpenScope g_stack[16];
uint32_t g_depth = 0;
uint64_t g_frameStart = 0;
uint32_t g_frameSerial = 0;
int32_t g_ready = 0;

int32_t Enabled() {
    if (g_shm) {
        return g_shm->enabled != 0;
    }
    return 1;
}

void Publish() {
    if (!g_shm || !g_frameStart) {
        return;
    }

    uint64_t now = OsGetAsyncTimeClocks();
    uint32_t slot = g_frameSerial % kFrames;
    uint32_t serial = g_shm->serial.load(std::memory_order_relaxed);
    g_shm->serial.store(serial + 1, std::memory_order_release);

    FrameRecord& frame = g_shm->frames[slot];
    frame.frameTicks = now - g_frameStart;
    frame.serial = g_frameSerial;
    memcpy(frame.zones, g_zones, sizeof(g_zones));
    g_history[slot] = frame;

    g_shm->serial.store(serial + 2, std::memory_order_release);
    g_frameSerial++;
    memset(g_zones, 0, sizeof(g_zones));
    g_frameStart = 0;
}

void OpenShm() {
#if defined(__linux__)
    int fd = shm_open("/wowdecomp-profile", O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        return;
    }

    if (ftruncate(fd, sizeof(SharedProfile)) != 0) {
        close(fd);
        return;
    }

    void* mapped = mmap(nullptr, sizeof(SharedProfile), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapped == MAP_FAILED) {
        return;
    }

    g_shm = static_cast<SharedProfile*>(mapped);
    if (g_shm->magic != kMagic || g_shm->version != kVersion || g_shm->zoneCount != Profiler::Count) {
        memset(g_shm, 0, sizeof(SharedProfile));
        g_shm->magic = kMagic;
        g_shm->version = kVersion;
        g_shm->zoneCount = Profiler::Count;
        g_shm->frameCapacity = kFrames;
        g_shm->nameBytes = kNameBytes;
        g_shm->clocksPerSecond = static_cast<uint64_t>(OsGetAsyncClocksPerSecond());
        g_shm->enabled = 1;
        new (&g_shm->serial) std::atomic<uint32_t>(0);
        for (uint32_t i = 0; i < Profiler::Count; i++) {
            strncpy(g_shm->names[i], kNames[i], kNameBytes - 1);
        }
    }

    const char* env = std::getenv("WOW_PROFILE");
    if (env && env[0] == '0' && env[1] == '\0') {
        g_shm->enabled = 0;
    }
#else
    (void)kMagic;
    (void)kVersion;
#endif
}

}

namespace Profiler {

int32_t Summarize(uint32_t frameCount, Summary& out) {
    memset(&out, 0, sizeof(out));

    if (!g_frameSerial) {
        return 0;
    }

    uint32_t available = g_frameSerial < kFrames ? g_frameSerial : kFrames;

    if (!frameCount || frameCount > available) {
        frameCount = available;
    }

    double clocks = static_cast<double>(OsGetAsyncClocksPerSecond());

    if (clocks < 1.0) {
        clocks = 1000000000.0;
    }

    uint32_t start = g_frameSerial - frameCount;
    uint64_t selfSum[Count] = {};
    uint64_t totalSum[Count] = {};
    uint32_t countSum[Count] = {};
    double frameSum = 0.0;

    for (uint32_t n = 0; n < frameCount; n++) {
        const FrameRecord& frame = g_history[(start + n) % kFrames];
        frameSum += static_cast<double>(frame.frameTicks);

        for (uint32_t zone = 0; zone < Count; zone++) {
            selfSum[zone] += frame.zones[zone].selfTicks;
            totalSum[zone] += frame.zones[zone].totalTicks;
            countSum[zone] += frame.zones[zone].count;
        }
    }

    out.frames = frameCount;
    out.frameMs = frameSum / static_cast<double>(frameCount) / clocks * 1000.0;

    for (uint32_t zone = 0; zone < Count; zone++) {
        out.zones[zone].name = kNames[zone];
        out.zones[zone].selfMs = static_cast<double>(selfSum[zone]) / static_cast<double>(frameCount) / clocks * 1000.0;
        out.zones[zone].totalMs = static_cast<double>(totalSum[zone]) / static_cast<double>(frameCount) / clocks * 1000.0;
        out.zones[zone].calls = static_cast<double>(countSum[zone]) / static_cast<double>(frameCount);
    }

    return 1;
}

Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {
    if (!g_ready) {
        OpenShm();
        g_ready = 1;
        g_enabled = !g_shm || g_shm->enabled != 0;
    } else if (g_depth == 0 && zone == WorldRender && g_shm) {
        g_enabled = g_shm->enabled != 0;
    }

    if (!g_enabled || zone >= Count || g_depth >= 16) {
        return;
    }

    if (g_depth == 0 && zone == WorldRender) {
        memset(g_zones, 0, sizeof(g_zones));
        g_frameStart = OsGetAsyncTimeClocks();
    }

    start = OsGetAsyncTimeClocks();
    g_stack[g_depth].id = id;
    g_stack[g_depth].start = start;
    g_stack[g_depth].child = 0;
    g_depth++;
}

Scope::~Scope() {
    if (!start || !g_depth) {
        return;
    }

    g_depth--;
    uint64_t inclusive = OsGetAsyncTimeClocks() - start;
    uint64_t self = inclusive - g_stack[g_depth].child;
    g_zones[id].selfTicks += self;
    g_zones[id].totalTicks += inclusive;
    g_zones[id].count++;

    if (g_depth) {
        g_stack[g_depth - 1].child += inclusive;
    } else if (id == WorldRender) {
        Publish();
    }
}

}
