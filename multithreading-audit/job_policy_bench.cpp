#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace Job {
void Publish(uint32_t count, RangeFn fn, void* user, uint32_t chunks, bool callerRuns);
}

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

struct Work {
    std::vector<uint64_t> output;
    unsigned distribution;
};

void Calculate(Job::Range range, void* user) {
    auto& work = *static_cast<Work*>(user);
    const uint32_t size = static_cast<uint32_t>(work.output.size());

    for (uint32_t i = range.begin; i < range.end; ++i) {
        unsigned loops = 16;
        if (work.distribution == 1) loops = 200;
        if (work.distribution == 2) loops = i < size / 32 ? 8000 : 50;
        uint64_t value = i + 1;
        for (unsigned n = 0; n < loops; ++n) {
            value = value * 6364136223846793005ULL + 1442695040888963407ULL;
            value ^= value >> 21;
        }
        work.output[i] = value;
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
    std::puts("items,distribution,chunks,sample,ms_per_batch");

    const uint32_t sizes[] = {32u, 128u, 512u, 4096u};
    for (uint32_t size : sizes) {
        for (unsigned distribution = 0; distribution < 3; ++distribution) {
            Work reference{std::vector<uint64_t>(size), distribution};
            Calculate({0, size}, &reference);
            Work work{std::vector<uint64_t>(size), distribution};
            uint32_t choices[4] = {
                std::min(size, workers),
                std::min(size, workers * 4u),
                std::min(size, workers * 8u),
                std::min(size, workers * 16u),
            };
            auto run = [&](uint32_t chunks) {
                Job::Publish(size, Calculate, &work, chunks, false);
                Job::Wait();
            };
            for (uint32_t chunks : choices) {
                run(chunks);
                if (work.output != reference.output) return 1;
            }
            for (unsigned sample = 0; sample < samples; ++sample) {
                std::shuffle(std::begin(choices), std::end(choices), random);
                for (uint32_t chunks : choices) {
                    const auto start = std::chrono::steady_clock::now();
                    for (unsigned iteration = 0; iteration < repeats; ++iteration) run(chunks);
                    const auto elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count();
                    if (work.output != reference.output) return 2;
                    std::printf("%u,%s,%u,%u,%.6f\n", size,
                        distribution == 0 ? "cheap" : distribution == 1 ? "uniform" : "skewed",
                        chunks, sample, elapsed / repeats);
                }
            }
        }
    }
    Job::Shutdown();
}
