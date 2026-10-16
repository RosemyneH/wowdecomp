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
    Count
};

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
