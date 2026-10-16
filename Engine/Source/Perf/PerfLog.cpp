#include "Engine/Source/Perf/PerfLog.h"

#include <cstdlib>

#include "Engine/Source/ConsoleBase/ConsoleCommand.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/Storm/H/storm.h"

PerfSlots g_perf = {};
std::atomic<uint8_t> g_perfOn = {};

static uint64_t g_windowStart = 0;
static char g_scene[16] = "none";
static char g_last[2048] = {};

static const char kPerfLine[] =
    "perf scene=%s dt_us=%llu frames=%llu z_frame_us=%llu z_rigid_us=%llu z_skin_us=%llu z_uni_us=%llu z_terrain_us=%llu z_grass_us=%llu z_stream_us=%llu z_vis_us=%llu z_cull_us=%llu stream_locks=%llu stream_bytes=%llu vk_alloc=%llu vk_bytes=%llu uni_upload=%llu uni_bytes=%llu desc_bind=%llu terrain_batch=%llu terrain_batch_max=%llu draws=%llu vis_flush=%llu rigid_locks=%llu rigid_bytes=%llu skin_locks=%llu skin_bytes=%llu inst_grow=%llu buf_destroy=%llu uni_pack=%llu terrain_cap=%llu terrain_bytes=%llu light_cap=%llu terrain_flush=%llu flush_alloc=%llu pair_scan=%llu terrain_draw_bytes=%llu grass_miss=%llu grass_fill=%llu grass_bytes=%llu stream_refill=%llu alloc_miss=%llu idx_write=%llu idx_bytes=%llu vis_jobs=%llu vis_bytes=%llu doodad_cull=%llu doodad_n=%llu vis_erase=%llu vk_heap=%llu vk_buf=%llu buf_new=%llu";

static uint64_t Take(std::atomic<uint64_t>& slot) {
    return slot.exchange(0, std::memory_order_relaxed);
}

static unsigned long long U(uint64_t value) {
    return static_cast<unsigned long long>(value);
}

static unsigned long long Us(uint64_t clocks, uint64_t hz) {
    return U(clocks * 1000000ull / hz);
}

static int32_t PerfSceneName(const char* arguments) {
    return !SStrCmp(arguments, "outdoor", sizeof("outdoor"))
        || !SStrCmp(arguments, "city", sizeof("city"))
        || !SStrCmp(arguments, "interior", sizeof("interior"))
        || !SStrCmp(arguments, "crowd", sizeof("crowd"));
}

static int32_t PerfLogCommand(const char*, const char* arguments) {
    if (!arguments || !arguments[0]) {
        if (!g_perfOn.load(std::memory_order_relaxed)) {
            ConsoleWrite("perf off", DEFAULT_COLOR);
        } else if (!g_last[0]) {
            ConsoleWrite("perf pending", DEFAULT_COLOR);
        } else {
            ConsoleWrite(g_last, DEFAULT_COLOR);
        }

        return 1;
    }

    if (PerfSceneName(arguments)) {
        SStrCopy(g_scene, arguments, sizeof(g_scene));
        return 1;
    }

    ConsoleWrite(g_scene, DEFAULT_COLOR);
    return 1;
}

void PerfLogInit() {
    const char* env = getenv("WOW_PERFLOG");
    g_perfOn.store(env && env[0] == '1' && env[1] == '\0' ? 1 : 0, std::memory_order_relaxed);
    ConsoleCommandRegister("perflog", PerfLogCommand, CATEGORY_DEBUG, "perflog [outdoor|city|interior|crowd]");
}

void PerfLogDestroy() {
    ConsoleCommandUnregister("perflog");
}

void PerfLogTick() {
    if (!g_perfOn.load(std::memory_order_relaxed)) {
        return;
    }

    uint64_t now = OsGetAsyncTimeClocks();

    if (g_windowStart == 0) {
        g_windowStart = now ? now : 1;
        // -- ʕ •ᴥ•ʔ✿ This render already added z_frame_clocks. Count it with them. ✿ ʕ •ᴥ•ʔ
        g_perf.frames.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    g_perf.frames.fetch_add(1, std::memory_order_relaxed);

    int64_t hzSigned = OsGetAsyncClocksPerSecond();

    if (hzSigned <= 0 || now - g_windowStart < static_cast<uint64_t>(hzSigned)) {
        return;
    }

    uint64_t hz = static_cast<uint64_t>(hzSigned);
    uint64_t elapsed = now - g_windowStart;
    uint64_t frames = Take(g_perf.frames);
    uint64_t zFrame = Take(g_perf.z_frame_clocks);
    uint64_t zRigid = Take(g_perf.z_rigid_clocks);
    uint64_t zSkin = Take(g_perf.z_skin_clocks);
    uint64_t zUni = Take(g_perf.z_uni_clocks);
    uint64_t zTerrain = Take(g_perf.z_terrain_clocks);
    uint64_t zGrass = Take(g_perf.z_grass_clocks);
    uint64_t zStream = Take(g_perf.z_stream_clocks);
    uint64_t zVis = Take(g_perf.z_vis_clocks);
    uint64_t zCull = Take(g_perf.z_cull_clocks);
    uint64_t streamLocks = Take(g_perf.stream_locks);
    uint64_t streamBytes = Take(g_perf.stream_bytes);
    uint64_t vkAlloc = Take(g_perf.vk_alloc);
    uint64_t vkBytes = Take(g_perf.vk_bytes);
    uint64_t uniUpload = Take(g_perf.uni_upload);
    uint64_t uniBytes = Take(g_perf.uni_bytes);
    uint64_t descBind = Take(g_perf.desc_bind);
    uint64_t terrainBatch = Take(g_perf.terrain_batch);
    uint64_t terrainBatchMax = Take(g_perf.terrain_batch_max);
    uint64_t draws = Take(g_perf.draws);
    uint64_t visFlush = Take(g_perf.vis_flush);
    uint64_t rigidLocks = Take(g_perf.rigid_locks);
    uint64_t rigidBytes = Take(g_perf.rigid_bytes);
    uint64_t skinLocks = Take(g_perf.skin_locks);
    uint64_t skinBytes = Take(g_perf.skin_bytes);
    uint64_t instGrow = Take(g_perf.inst_grow);
    uint64_t bufDestroy = Take(g_perf.buf_destroy);
    uint64_t uniPack = Take(g_perf.uni_pack);
    uint64_t terrainCap = Take(g_perf.terrain_cap);
    uint64_t terrainBytes = Take(g_perf.terrain_bytes);
    uint64_t lightCap = Take(g_perf.light_cap);
    uint64_t terrainFlush = Take(g_perf.terrain_flush);
    uint64_t flushAlloc = Take(g_perf.flush_alloc);
    uint64_t pairScan = Take(g_perf.pair_scan);
    uint64_t terrainDrawBytes = Take(g_perf.terrain_draw_bytes);
    uint64_t grassMiss = Take(g_perf.grass_miss);
    uint64_t grassFill = Take(g_perf.grass_fill);
    uint64_t grassBytes = Take(g_perf.grass_bytes);
    uint64_t streamRefill = Take(g_perf.stream_refill);
    uint64_t allocMiss = Take(g_perf.alloc_miss);
    uint64_t idxWrite = Take(g_perf.idx_write);
    uint64_t idxBytes = Take(g_perf.idx_bytes);
    uint64_t visJobs = Take(g_perf.vis_jobs);
    uint64_t visBytes = Take(g_perf.vis_bytes);
    uint64_t doodadCull = Take(g_perf.doodad_cull);
    uint64_t doodadN = Take(g_perf.doodad_n);
    uint64_t visErase = Take(g_perf.vis_erase);
    uint64_t vkHeap = Take(g_perf.vk_heap);
    uint64_t vkBuf = Take(g_perf.vk_buf);
    uint64_t bufNew = Take(g_perf.buf_new);

    char line[2048];
    SStrPrintf(
        line,
        sizeof(line),
        kPerfLine,
        g_scene,
        Us(elapsed, hz),
        U(frames),
        Us(zFrame, hz),
        Us(zRigid, hz),
        Us(zSkin, hz),
        Us(zUni, hz),
        Us(zTerrain, hz),
        Us(zGrass, hz),
        Us(zStream, hz),
        Us(zVis, hz),
        Us(zCull, hz),
        U(streamLocks),
        U(streamBytes),
        U(vkAlloc),
        U(vkBytes),
        U(uniUpload),
        U(uniBytes),
        U(descBind),
        U(terrainBatch),
        U(terrainBatchMax),
        U(draws),
        U(visFlush),
        U(rigidLocks),
        U(rigidBytes),
        U(skinLocks),
        U(skinBytes),
        U(instGrow),
        U(bufDestroy),
        U(uniPack),
        U(terrainCap),
        U(terrainBytes),
        U(lightCap),
        U(terrainFlush),
        U(flushAlloc),
        U(pairScan),
        U(terrainDrawBytes),
        U(grassMiss),
        U(grassFill),
        U(grassBytes),
        U(streamRefill),
        U(allocMiss),
        U(idxWrite),
        U(idxBytes),
        U(visJobs),
        U(visBytes),
        U(doodadCull),
        U(doodadN),
        U(visErase),
        U(vkHeap),
        U(vkBuf),
        U(bufNew));
    SStrCopy(g_last, line, sizeof(g_last));
    ConsoleWrite(line, DEFAULT_COLOR);
    g_windowStart = now ? now : 1;
}
