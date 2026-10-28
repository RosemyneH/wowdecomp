#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Job {
void Publish(uint32_t count, RangeFn fn, void* user, uint32_t chunks);
}

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

struct Work {
    std::vector<uint64_t> output;
    uint32_t groups;
    unsigned distribution;
};

void Calculate(Job::Range range, void* user) {
    auto& work = *static_cast<Work*>(user);
    const uint32_t size = static_cast<uint32_t>(work.output.size());
    for (uint32_t group = range.begin; group < range.end; ++group) {
        const uint32_t begin = static_cast<uint64_t>(group) * size / work.groups;
        const uint32_t end = static_cast<uint64_t>(group + 1) * size / work.groups;
        for (uint32_t i = begin; i < end; ++i) {
            unsigned loops = 200;
            if (work.distribution == 1) loops = 50 + (i % 16) * 40;
            if (work.distribution == 2) loops = i < size / 32 ? 8000 : 50;
            uint64_t value = i + 1;
            for (unsigned n = 0; n < loops; ++n) {
                value = value * 6364136223846793005ULL + 1442695040888963407ULL;
                value ^= value >> 21;
            }
            work.output[i] = value;
        }
    }
}

int main() {
    constexpr uint32_t size = 4096;
    constexpr unsigned repeats = 20;
    Job::Init();
    const uint32_t workers = Job::WorkerCount();
    std::printf("workers=%u items=%u batches_per_sample=%u samples=5\n", workers, size, repeats);
    for (unsigned distribution = 0; distribution < 3; ++distribution) {
        Work reference{std::vector<uint64_t>(size), 1, distribution};
        Calculate({0, 1}, &reference);
        for (unsigned multiplier : {1u, 2u, 4u, 8u}) {
            Work work{std::vector<uint64_t>(size), size, distribution};
            const uint32_t chunks = std::min(size, workers * multiplier);
            Job::Publish(size, Calculate, &work, chunks);
            Job::Wait();
            if (work.output != reference.output) return 1;
            std::printf("distribution=%s groups=%u ms_per_batch=", distribution == 0 ? "uniform" : distribution == 1 ? "uneven" : "skewed", chunks);
            for (unsigned sample = 0; sample < 5; ++sample) {
                const auto start = std::chrono::steady_clock::now();
                for (unsigned iteration = 0; iteration < repeats; ++iteration) {
                    Job::Publish(size, Calculate, &work, chunks);
                    Job::Wait();
                }
                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                if (work.output != reference.output) return 2;
                std::printf("%s%.3f", sample ? "," : "", elapsed / repeats);
            }
            std::puts("");
        }
    }
    Job::Shutdown();
}
