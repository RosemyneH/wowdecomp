#pragma once
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
void ParallelFor(uint32_t count, RangeFn fn, void* user);
}
