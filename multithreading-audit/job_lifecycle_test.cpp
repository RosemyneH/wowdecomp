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

struct MultiBlockingJob {
    std::atomic<unsigned> entered{0};
    std::atomic<unsigned> completed{0};
    std::atomic<bool> release{false};
};

void BlockMultiple(Job::Range, void* user) {
    auto& job = *static_cast<MultiBlockingJob*>(user);
    ++job.entered;
    while (!job.release.load()) std::this_thread::yield();
    ++job.completed;
}

void Count(Job::Range range, void* user) {
    auto* counts = static_cast<std::atomic<unsigned>*>(user);
    for (uint32_t i = range.begin; i < range.end; ++i) {
        ++counts[i];
    }
}

int main() {
    Job::ParallelFor(1, nullptr, nullptr);
    Job::Start(1, nullptr, nullptr);
    std::atomic<unsigned> inlineCount{0};
    Job::Start(1, Count, &inlineCount);
    Job::ParallelFor(1, Count, &inlineCount);
    Job::Wait();
    Job::Shutdown();
    if (inlineCount.load() != 2) return 4;
    Job::Init();
    Job::ParallelFor(0, nullptr, nullptr);
    Job::ParallelFor(1, nullptr, nullptr);
    Job::Start(1, nullptr, nullptr);
    BlockingJob job;
    Job::Start(1, Block, &job);
    while (!job.entered.load()) {
        std::this_thread::yield();
    }
    Job::ParallelFor(0, Block, &job);
    Job::ParallelFor(1, nullptr, nullptr);
    Job::Start(0, Block, &job);
    Job::Start(1, nullptr, nullptr);
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        job.release.store(true);
    });
    Job::Shutdown();
    release.join();
    if (job.calls.load() != 1) return 1;
    Job::Wait();

    Job::Init();
    for (unsigned mode = 0; mode < 2; ++mode) {
        BlockingJob prior;
        std::atomic<unsigned> next{0};
        Job::Start(1, Block, &prior);
        while (!prior.entered.load()) std::this_thread::yield();
        std::thread unblock([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            prior.release.store(true);
        });
        if (mode == 0) Job::Start(1, Count, &next);
        else Job::ParallelFor(1, Count, &next);
        bool priorCompleted = prior.calls.load() == 1;
        unblock.join();
        if (!priorCompleted) return 5;
        Job::Wait();
        if (next.load() != 1) return 6;
    }
    if (Job::WorkerCount() >= 2) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            MultiBlockingJob multiple;
            Job::Start(2, BlockMultiple, &multiple);
            while (multiple.entered.load() != 2) std::this_thread::yield();
            std::thread unblock([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                multiple.release.store(true);
            });
            if (mode == 0) Job::Wait();
            else Job::Shutdown();
            bool bothCompleted = multiple.completed.load() == 2;
            unblock.join();
            if (!bothCompleted) return 7;
            if (mode == 1) Job::Init();
        }
        std::puts("multiple active callbacks passed");
    } else {
        std::puts("multiple active callbacks skipped: fewer than two workers");
    }
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
