#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace {

struct Pool {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::thread> threads;
    uint32_t workerCount = 0;
    uint32_t epoch = 0;
    uint32_t finished = 0;
    bool shutdown = false;
    Job::RangeFn fn = nullptr;
    void* user = nullptr;
    uint32_t count = 0;
    uint32_t chunks = 0;
    std::atomic<uint32_t> next{0};
    std::atomic<uint32_t> remaining{0};
    bool active = false;
};

Pool g_pool;

// -- ʕ •ᴥ•ʔ✿ Two cores stay with the OS and the main thread. ✿ ʕ •ᴥ•ʔ
uint32_t ReserveWorkerCount() {
    unsigned cores = std::thread::hardware_concurrency();
    if (cores > 2) {
        return static_cast<uint32_t>(cores - 2);
    }
    return 1;
}

void RunRanges(Job::RangeFn fn, void* user, uint32_t count, uint32_t chunks) {
    uint32_t base = count / chunks;
    uint32_t extra = count % chunks;

    for (;;) {
        uint32_t index = g_pool.next.fetch_add(1);
        if (index >= chunks) {
            return;
        }

        Job::Range range;
        range.begin = index * base + (index < extra ? index : extra);
        range.end = range.begin + base + (index < extra ? 1u : 0u);
        fn(range, user);
        g_pool.remaining.fetch_sub(1);
    }
}

void WorkerMain() {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "wow-job");
#endif

    std::unique_lock<std::mutex> lock(g_pool.mu);
    uint32_t seen = 0;

    for (;;) {
        g_pool.cv.wait(lock, [&] {
            return g_pool.shutdown || g_pool.epoch != seen;
        });
        if (g_pool.shutdown) {
            return;
        }

        seen = g_pool.epoch;
        Job::RangeFn fn = g_pool.fn;
        void* user = g_pool.user;
        uint32_t count = g_pool.count;
        uint32_t chunks = g_pool.chunks;
        lock.unlock();
        RunRanges(fn, user, count, chunks);
        lock.lock();
        g_pool.finished += 1;
        g_pool.cv.notify_all();
    }
}

}

namespace Job {

uint32_t WorkerCount() {
    return g_pool.workerCount;
}

void Init() {
    std::lock_guard<std::mutex> lock(g_pool.mu);
    if (!g_pool.threads.empty()) {
        return;
    }

    g_pool.workerCount = ReserveWorkerCount();
    g_pool.shutdown = false;
    g_pool.threads.reserve(g_pool.workerCount);
    for (uint32_t index = 0; index < g_pool.workerCount; ++index) {
        g_pool.threads.emplace_back(WorkerMain);
    }
}

void Shutdown() {
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(g_pool.mu);
        if (g_pool.threads.empty()) {
            return;
        }
        g_pool.shutdown = true;
        g_pool.cv.notify_all();
        threads.swap(g_pool.threads);
        g_pool.workerCount = 0;
    }

    for (std::thread& thread : threads) {
        thread.join();
    }

    g_pool.shutdown = false;
    g_pool.epoch = 0;
}

void Publish(uint32_t count, RangeFn fn, void* user, uint32_t chunks) {
    std::lock_guard<std::mutex> lock(g_pool.mu);
    g_pool.fn = fn;
    g_pool.user = user;
    g_pool.count = count;
    g_pool.chunks = chunks;
    g_pool.next.store(0);
    g_pool.remaining.store(chunks);
    g_pool.finished = 0;
    g_pool.active = true;
    g_pool.epoch += 1;
    g_pool.cv.notify_all();
}

void Join() {
    Profiler::Scope profile(Profiler::JobJoin);
    std::unique_lock<std::mutex> lock(g_pool.mu);
    if (!g_pool.active) {
        return;
    }

    // -- ʕ •ᴥ•ʔ✿ Stay until ranges finish and workers drop fn, so the next publish cannot race them. ✿ ʕ •ᴥ•ʔ
    g_pool.cv.wait(lock, [] {
        return g_pool.remaining.load() == 0 && g_pool.finished == g_pool.workerCount;
    });
    g_pool.active = false;
    g_pool.fn = nullptr;
}

void Wait() {
    RangeFn fn = nullptr;
    void* user = nullptr;
    uint32_t count = 0;
    uint32_t chunks = 0;

    {
        std::lock_guard<std::mutex> lock(g_pool.mu);
        if (!g_pool.active) {
            return;
        }

        fn = g_pool.fn;
        user = g_pool.user;
        count = g_pool.count;
        chunks = g_pool.chunks;
    }

    if (fn && chunks) {
        RunRanges(fn, user, count, chunks);
    }

    Join();
}

void Start(uint32_t count, RangeFn fn, void* user) {
    if (!count || !fn) {
        return;
    }

    Wait();

    if (!g_pool.workerCount) {
        fn(Range{0, count}, user);
        return;
    }

    uint32_t chunks = count < g_pool.workerCount ? count : g_pool.workerCount;
    Publish(count, fn, user, chunks);
}

void ParallelFor(uint32_t count, RangeFn fn, void* user) {
    if (count == 0) {
        return;
    }

    Wait();

    if (!g_pool.workerCount) {
        fn(Range{0, count}, user);
        return;
    }

    // -- ʕ •ᴥ•ʔ✿ One chunk per worker plus the caller, near WorkerCount, not one atomic per item. ✿ ʕ •ᴥ•ʔ
    uint32_t chunks = g_pool.workerCount + 1;
    if (chunks > count) {
        chunks = count;
    }

    Publish(count, fn, user, chunks);
    RunRanges(fn, user, count, chunks);
    Join();
}

}
