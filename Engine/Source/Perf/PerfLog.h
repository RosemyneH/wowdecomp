#ifndef ENGINE_SOURCE_PERF_PERFLOG_H
#define ENGINE_SOURCE_PERF_PERFLOG_H

#include <atomic>
#include <cstdint>
#include "Engine/Source/Os/TimeManager.h"

struct PerfSlots {
    std::atomic<uint64_t> frames;
    std::atomic<uint64_t> z_frame_clocks;
    std::atomic<uint64_t> z_rigid_clocks;
    std::atomic<uint64_t> z_skin_clocks;
    std::atomic<uint64_t> z_uni_clocks;
    std::atomic<uint64_t> z_terrain_clocks;
    std::atomic<uint64_t> z_grass_clocks;
    std::atomic<uint64_t> z_stream_clocks;
    std::atomic<uint64_t> z_vis_clocks;
    std::atomic<uint64_t> z_cull_clocks;
    std::atomic<uint64_t> stream_locks;
    std::atomic<uint64_t> stream_bytes;
    std::atomic<uint64_t> vk_alloc;
    std::atomic<uint64_t> vk_bytes;
    std::atomic<uint64_t> uni_upload;
    std::atomic<uint64_t> uni_bytes;
    std::atomic<uint64_t> desc_bind;
    std::atomic<uint64_t> terrain_batch;
    std::atomic<uint64_t> terrain_batch_max;
    std::atomic<uint64_t> draws;
    std::atomic<uint64_t> vis_flush;
    std::atomic<uint64_t> rigid_locks;
    std::atomic<uint64_t> rigid_bytes;
    std::atomic<uint64_t> skin_locks;
    std::atomic<uint64_t> skin_bytes;
    std::atomic<uint64_t> inst_grow;
    std::atomic<uint64_t> buf_destroy;
    std::atomic<uint64_t> uni_pack;
    std::atomic<uint64_t> terrain_cap;
    std::atomic<uint64_t> terrain_bytes;
    std::atomic<uint64_t> light_cap;
    std::atomic<uint64_t> terrain_flush;
    std::atomic<uint64_t> flush_alloc;
    std::atomic<uint64_t> pair_scan;
    std::atomic<uint64_t> terrain_draw_bytes;
    std::atomic<uint64_t> grass_miss;
    std::atomic<uint64_t> stream_refill;
    std::atomic<uint64_t> alloc_miss;
    std::atomic<uint64_t> vis_jobs;
    std::atomic<uint64_t> vis_bytes;
    std::atomic<uint64_t> doodad_cull;
    std::atomic<uint64_t> doodad_n;
    std::atomic<uint64_t> vis_erase;
    std::atomic<uint64_t> vk_heap;
    std::atomic<uint64_t> vk_buf;
    std::atomic<uint64_t> buf_new;
    // -- ʕ •ᴥ•ʔ✿ Each worker slot owns a line so ParallelFor increments do not share it ✿ ʕ •ᴥ•ʔ
    alignas(64) std::atomic<uint64_t> grass_fill;
    alignas(64) std::atomic<uint64_t> grass_bytes;
    alignas(64) std::atomic<uint64_t> idx_write;
    alignas(64) std::atomic<uint64_t> idx_bytes;
};

extern PerfSlots g_perf;
extern std::atomic<uint8_t> g_perfOn;

inline void PerfAdd(std::atomic<uint64_t>& slot, uint64_t n) {
    if (!g_perfOn.load(std::memory_order_relaxed)) {
        return;
    }

    slot.fetch_add(n, std::memory_order_relaxed);
}

struct PerfZone {
    std::atomic<uint64_t>* slot;
    uint64_t start;

    explicit PerfZone(std::atomic<uint64_t>& s) : slot(&s), start(0) {
        if (g_perfOn.load(std::memory_order_relaxed)) {
            start = OsGetAsyncTimeClocks();
        }
    }

    ~PerfZone() {
        if (!start) {
            return;
        }

        slot->fetch_add(OsGetAsyncTimeClocks() - start, std::memory_order_relaxed);
    }

    PerfZone(const PerfZone&) = delete;
    PerfZone& operator=(const PerfZone&) = delete;
};

void PerfLogInit();
void PerfLogDestroy();
void PerfLogTick();

#endif
