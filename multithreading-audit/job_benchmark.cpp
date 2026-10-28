#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <array>
#include <vector>

namespace Job {
void Publish(uint32_t count, RangeFn fn, void* user, uint32_t chunks);
}

// -- ʕ •ᴥ•ʔ✿ Same clamp as JobSystem.cpp ChunkCount: four ranges per participant ✿ ʕ •ᴥ•ʔ
uint32_t ReportedChunks(uint32_t size, uint32_t workers, unsigned multiplier) {
    if (!size) {
        return 0;
    }
    if (multiplier == 0) {
        uint64_t chunks = static_cast<uint64_t>(workers + 1) * 4u;
        return static_cast<uint32_t>(chunks > size ? size : chunks);
    }
    return std::min(size, workers * multiplier);
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
    constexpr unsigned repeats = 20;
    constexpr unsigned samples = 5;
    constexpr unsigned seed = 335;
    std::mt19937 random(seed);
    Job::Init();
    const uint32_t workers = Job::WorkerCount();
    std::printf("workers=%u batches_per_sample=%u samples=%u seed=%u\n", workers, repeats, samples, seed);
    std::puts("items,distribution,path,chunks,sample,ms_per_batch");
    for (uint32_t size : {0u, 1u, 8u, 4096u}) {
        for (unsigned distribution = 0; distribution < 3; ++distribution) {
            Work reference{std::vector<uint64_t>(size), size, distribution};
            if (size) Calculate({0, size}, &reference);
            Work work{std::vector<uint64_t>(size), size, distribution};
            std::array<unsigned, 5> multipliers{0, 1, 2, 4, 8};
            auto run = [&](unsigned multiplier) {
                if (multiplier == 0 || size == 0) {
                    Job::ParallelFor(size, Calculate, &work);
                } else {
                    Job::Publish(size, Calculate, &work, std::min(size, workers * multiplier));
                    Job::Wait();
                }
            };
            for (unsigned multiplier : multipliers) {
                run(multiplier);
                if (work.output != reference.output) return 1;
            }
            for (unsigned sample = 0; sample < samples; ++sample) {
                std::shuffle(multipliers.begin(), multipliers.end(), random);
                for (unsigned multiplier : multipliers) {
                    // Empty dispatch has no internal Publish equivalent: measure only the API.
                    if (!size && multiplier) continue;
                    const auto start = std::chrono::steady_clock::now();
                    for (unsigned iteration = 0; iteration < repeats; ++iteration) run(multiplier);
                    const auto elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
                    if (work.output != reference.output) return 2;
                    std::printf("%u,%s,%s,%u,%u,%.6f\n", size,
                        distribution == 0 ? "uniform" : distribution == 1 ? "uneven" : "skewed",
                        multiplier == 0 ? "parallel_for" : "publish_wait",
                        ReportedChunks(size, workers, multiplier),
                        sample, elapsed / repeats);
                }
            }
        }
    }
    Job::Shutdown();
}
