#ifndef ENGINE_SOURCE_BASE_JOB_JOBSYSTEM_H
#define ENGINE_SOURCE_BASE_JOB_JOBSYSTEM_H

#include <cstdint>

namespace Job {

struct Range {
    uint32_t begin;
    uint32_t end;
};

using RangeFn = void (*)(Range range, void* user);

void Init();
void Shutdown();
uint32_t WorkerCount();

// Main thread only. Splits [0, count) across the pool and the calling thread.
// Returns when every range has finished. fn must not call Job:: or anything on g_theGxDevicePtr.
void ParallelFor(uint32_t count, RangeFn fn, void* user);

// Main thread only. Wakes the pool and returns. The caller can draw while the workers run.
// Wait joins that work. A later ParallelFor also joins it. fn has the same limits as ParallelFor.
void Start(uint32_t count, RangeFn fn, void* user);
void Wait();

}

#endif
