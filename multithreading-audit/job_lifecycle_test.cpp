#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

Profiler::Scope::Scope(uint32_t zone) : id(zone), start(0), child(0) {}
Profiler::Scope::~Scope() {}

struct BlockingJob {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<unsigned> calls{0};
};

void Block(Job::Range, void* user) {
    auto& job = *static_cast<BlockingJob*>(user);
    job.entered.store(true);
    while (!job.release.load()) {
        std::this_thread::yield();
    }
    ++job.calls;
}

void Count(Job::Range range, void* user) {
    auto* counts = static_cast<std::atomic<unsigned>*>(user);
    for (uint32_t i = range.begin; i < range.end; ++i) {
        ++counts[i];
    }
}

int main() {
    Job::Init();
    BlockingJob job;
    Job::Start(1, Block, &job);
    while (!job.entered.load()) {
        std::this_thread::yield();
    }
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        job.release.store(true);
    });
    Job::Shutdown();
    release.join();
    if (job.calls.load() != 1) return 1;
    Job::Wait();

    Job::Init();
    std::vector<std::atomic<unsigned>> counts(257);
    for (auto& count : counts) count.store(0);
    for (unsigned i = 0; i < 30; ++i) {
        Job::Start(static_cast<uint32_t>(counts.size()), Count, counts.data());
        Job::Wait();
        Job::ParallelFor(static_cast<uint32_t>(counts.size()), Count, counts.data());
    }
    Job::Shutdown();
    for (const auto& count : counts) {
        if (count.load() != 60) return 2;
    }
    for (unsigned iteration = 0; iteration < 10; ++iteration) {
        for (auto& count : counts) count.store(0);
        Job::Init();
        Job::Start(static_cast<uint32_t>(counts.size()), Count, counts.data());
        Job::Shutdown();
        for (const auto& count : counts) {
            if (count.load() != 1) return 3;
        }
        Job::Wait();
    }
    std::puts("job lifecycle passed");
}
