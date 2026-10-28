#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <limits>
#include <mutex>
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

void WriteRange(Job::Range range, void* user) {
    auto* values = static_cast<uint32_t*>(user);
    for (uint32_t i = range.begin; i < range.end; ++i) values[i] = i + 1;
}

struct RangeCoverage {
    std::vector<std::atomic<unsigned>> counts;
    std::atomic<bool> invalid{false};

    explicit RangeCoverage(uint32_t size) : counts(size) {
        for (auto& count : counts) count.store(0);
    }
};

void CheckRange(Job::Range range, void* user) {
    auto& coverage = *static_cast<RangeCoverage*>(user);
    if (range.begin >= range.end || range.end > coverage.counts.size()) {
        coverage.invalid.store(true);
        return;
    }
    for (uint32_t i = range.begin; i < range.end; ++i) ++coverage.counts[i];
}

struct CollectedRanges {
    std::mutex mutex;
    std::vector<Job::Range> ranges;
};

void CollectRange(Job::Range range, void* user) {
    auto& collected = *static_cast<CollectedRanges*>(user);
    std::lock_guard<std::mutex> lock(collected.mutex);
    collected.ranges.push_back(range);
}

bool CheckInlineDispatch() {
    if (Job::WorkerCount() != 0) return false;
    for (unsigned mode = 0; mode < 2; ++mode) {
        CollectedRanges collected;
        const uint32_t size = std::numeric_limits<uint32_t>::max();
        if (mode == 0) Job::Start(size, CollectRange, &collected);
        else Job::ParallelFor(size, CollectRange, &collected);
        if (collected.ranges.size() != 1 || collected.ranges[0].begin != 0 ||
            collected.ranges[0].end != size) return false;
    }
    return true;
}

int main() {
    if (!CheckInlineDispatch()) return 15;
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
    const uint32_t initialWorkers = Job::WorkerCount();
    Job::Init();
    if (Job::WorkerCount() != initialWorkers || job.calls.load() != 0) return 13;
    std::thread release([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        job.release.store(true);
    });
    Job::Shutdown();
    release.join();
    if (job.calls.load() != 1) return 1;
    Job::Wait();

    Job::Init();
    const uint32_t workersBeforeRepeatedInit = Job::WorkerCount();
    Job::Init();
    if (Job::WorkerCount() != workersBeforeRepeatedInit) return 14;
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
    Job::Init();
    for (unsigned mode = 0; mode < 3; ++mode) {
        for (uint32_t size : {1u, 3u, 257u, 1025u}) {
            std::vector<uint32_t> values(size, 0);
            if (mode == 1) {
                Job::ParallelFor(size, WriteRange, values.data());
            } else {
                Job::Start(size, WriteRange, values.data());
                if (mode == 0) Job::Wait();
                else Job::Shutdown();
            }
            for (uint32_t i = 0; i < size; ++i) {
                if (values[i] != i + 1) return 8;
            }
            if (mode == 2) Job::Init();
        }
    }
    const uint32_t workers = Job::WorkerCount();
    for (uint32_t size : {1u, 2u, workers, workers + 1, workers * 2 + 3, 1025u}) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            RangeCoverage coverage(size);
            if (mode == 0) {
                Job::Start(size, CheckRange, &coverage);
                Job::Wait();
            } else {
                Job::ParallelFor(size, CheckRange, &coverage);
            }
            if (coverage.invalid.load()) return 9;
            for (const auto& count : coverage.counts) {
                if (count.load() != 1) return 10;
            }
        }
    }
    for (uint32_t size : {std::numeric_limits<uint32_t>::max() - 1,
                          std::numeric_limits<uint32_t>::max()}) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            CollectedRanges collected;
            if (mode == 0) {
                Job::Start(size, CollectRange, &collected);
                Job::Wait();
            } else {
                Job::ParallelFor(size, CollectRange, &collected);
            }
            std::sort(collected.ranges.begin(), collected.ranges.end(),
                      [](Job::Range a, Job::Range b) { return a.begin < b.begin; });
            uint32_t end = 0;
            for (Job::Range range : collected.ranges) {
                if (range.begin != end || range.end <= range.begin) return 11;
                end = range.end;
            }
            if (end != size) return 12;
        }
    }
    Job::Shutdown();
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
    if (!CheckInlineDispatch()) return 16;
    std::puts("job lifecycle passed");
}
