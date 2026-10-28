#include "Engine/Source/Base/Job/JobSystem.h"
#include "Engine/Source/Base/Profiler.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

namespace {

struct WorkerSlot {
    std::condition_variable cv;
    std::thread thread;
    bool run = false;
};

struct Pool {
    std::mutex mu;
    std::condition_variable joinCv;
    std::vector<std::unique_ptr<WorkerSlot>> workers;
    uint32_t workerCount = 0;
    uint32_t wakeCursor = 0;
    uint32_t released = 0;
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

// -- ʕ •ᴥ•ʔ✿ Four claims per participant. One range per worker pins a heavy model to its neighbors ✿ ʕ •ᴥ•ʔ
uint32_t ChunkCount(uint32_t count, uint32_t participants) {
    uint64_t chunks = static_cast<uint64_t>(participants) * 4u;
    if (chunks > count) {
        chunks = count;
    }
    return static_cast<uint32_t>(chunks);
}

// Pooled callbacks must not unwind, including on a participating caller.
// Otherwise a partial batch could escape without completion accounting.
void RunRanges(Job::RangeFn fn, void* user, uint32_t count, uint32_t chunks) noexcept {
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

void WorkerMain(uint32_t index) {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), "wow-job");
#endif

    std::unique_lock<std::mutex> lock(g_pool.mu);
    WorkerSlot& self = *g_pool.workers[index];

    for (;;) {
        self.cv.wait(lock, [&] {
            return g_pool.shutdown || self.run;
        });
        if (g_pool.shutdown) {
            return;
        }

        self.run = false;
        Job::RangeFn fn = g_pool.fn;
        void* user = g_pool.user;
        uint32_t count = g_pool.count;
        uint32_t chunks = g_pool.chunks;
        lock.unlock();
        RunRanges(fn, user, count, chunks);
        lock.lock();
        g_pool.finished += 1;
        g_pool.joinCv.notify_one();
    }
}

void WakeWorkers(uint32_t wake) {
    uint32_t workerCount = g_pool.workerCount;
    g_pool.released = wake;

    for (uint32_t n = 0; n < wake; ++n) {
        uint32_t index = g_pool.wakeCursor;
        if (++g_pool.wakeCursor == workerCount) {
            g_pool.wakeCursor = 0;
        }
        WorkerSlot& slot = *g_pool.workers[index];
        slot.run = true;
        slot.cv.notify_one();
    }
}

void JoinSpawned(std::unique_lock<std::mutex>& lock) {
    lock.unlock();
    for (std::unique_ptr<WorkerSlot>& slot : g_pool.workers) {
        if (slot->thread.joinable()) {
            slot->thread.join();
        }
    }
    lock.lock();
    g_pool.workers.clear();
    g_pool.workerCount = 0;
    g_pool.released = 0;
    g_pool.wakeCursor = 0;
    g_pool.shutdown = false;
}

}

namespace Job {

uint32_t WorkerCount() {
    return g_pool.workerCount;
}

void Init() {
    std::unique_lock<std::mutex> lock(g_pool.mu);
    if (!g_pool.workers.empty()) {
        return;
    }

    uint32_t workerCount = ReserveWorkerCount();
    g_pool.shutdown = false;
    try {
        g_pool.workers.reserve(workerCount);
        for (uint32_t index = 0; index < workerCount; ++index) {
            g_pool.workers.push_back(std::make_unique<WorkerSlot>());
            g_pool.workers.back()->thread = std::thread(WorkerMain, index);
        }
    } catch (...) {
        g_pool.shutdown = true;
        for (std::unique_ptr<WorkerSlot>& slot : g_pool.workers) {
            slot->cv.notify_one();
        }
        JoinSpawned(lock);
        throw;
    }
    g_pool.workerCount = workerCount;
}

void Shutdown() {
    Wait();
    std::unique_lock<std::mutex> lock(g_pool.mu);
    if (g_pool.workers.empty()) {
        return;
    }

    g_pool.shutdown = true;
    for (std::unique_ptr<WorkerSlot>& slot : g_pool.workers) {
        slot->cv.notify_one();
    }
    JoinSpawned(lock);
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
    uint32_t wake = chunks < g_pool.workerCount ? chunks : g_pool.workerCount;
    WakeWorkers(wake);
}

void Join() {
    Profiler::Scope profile(Profiler::JobJoin);
    std::unique_lock<std::mutex> lock(g_pool.mu);
    if (!g_pool.active) {
        return;
    }

    // -- ʕ •ᴥ•ʔ✿ Stay until ranges finish and the woken workers drop fn, so the next publish cannot race them. ✿ ʕ •ᴥ•ʔ
    g_pool.joinCv.wait(lock, [] {
        return g_pool.remaining.load() == 0 && g_pool.finished == g_pool.released;
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

    Publish(count, fn, user, ChunkCount(count, g_pool.workerCount));
}

void ParallelFor(uint32_t count, RangeFn fn, void* user) {
    if (!count || !fn) {
        return;
    }

    Wait();

    if (!g_pool.workerCount) {
        fn(Range{0, count}, user);
        return;
    }

    uint32_t chunks = ChunkCount(count, g_pool.workerCount + 1);
    Publish(count, fn, user, chunks);
    RunRanges(fn, user, count, chunks);
    Join();
}

}
