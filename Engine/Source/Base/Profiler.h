#ifndef ENGINE_SOURCE_BASE_PROFILER_H
#define ENGINE_SOURCE_BASE_PROFILER_H

#include <cstdint>

namespace Profiler {

enum Id : uint32_t {
    WorldRender = 0,
    SceneRender,
    TerrainFlush,
    VisFlush,
    M2Draw,
    GxDraw,
    JobJoin,
    Idle,
    WorldUpdate,
    WorldAfter,
    Ui,
    Present,
    Count
};

void BeginFrame();
void EndFrame();

struct ZoneLine {
    const char* name;
    double selfMs;
    double totalMs;
    double calls;
};

struct Summary {
    double frameMs;
    uint32_t frames;
    ZoneLine zones[Count];
};

int32_t Summarize(uint32_t frameCount, Summary& out);

struct Scope {
    uint32_t id;
    uint64_t start;
    uint64_t child;

    explicit Scope(uint32_t zone);
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}

#endif
