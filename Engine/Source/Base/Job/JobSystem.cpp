#include "JobSystem.h"

#include "Engine/Source/Base/OsPosix.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace Job {
namespace {

struct Pool {
    std::mutex mutex;
    std::condition_variable workCv;
    std::condition_variable doneCv;
    std::vector<std::thread> threads;
    RangeFn fn = nullptr;
    void* user = nullptr;
    uint32_t chunkSize = 0;
    uint32_t remainder = 0;
    uint32_t workerChunks = 0;
    std::atomic<uint32_t> next{0};
    std::atomic<uint32_t> unfinished{0};
    uint32_t epoch = 0;
    bool stop = false;
    bool busy = false;
    std::thread::id owner{};
};

Pool* g_pool = nullptr;

uint32_t ChooseWorkers() {
    unsigned cores = std::thread::hardware_concurrency();

    if (cores > 2) {
        return static_cast<uint32_t>(cores - 2);
    }

    return 1;
}

Range ChunkRange(const Pool& pool, uint32_t index) {
    uint32_t begin = index * pool.chunkSize + (index < pool.remainder ? index : pool.remainder);
    uint32_t count = pool.chunkSize + (index < pool.remainder ? 1u : 0u);
    return Range{begin, begin + count};
}

void RunChunks(Pool& pool, uint32_t limit) {
    uint32_t index = pool.next.fetch_add(1, std::memory_order_relaxed);

    while (index < limit) {
        pool.fn(ChunkRange(pool, index), pool.user);
        index = pool.next.fetch_add(1, std::memory_order_relaxed);
    }
}

void WorkerLoop(Pool* pool) {
    OsPosixSetThreadName("wow-job");

    uint32_t seen = 0;
    std::unique_lock<std::mutex> lock(pool->mutex);

    while (!pool->stop) {
        pool->workCv.wait(lock, [&] { return pool->stop || pool->epoch != seen; });

        if (pool->stop) {
            return;
        }

        seen = pool->epoch;
        lock.unlock();
        RunChunks(*pool, pool->workerChunks);

        if (pool->unfinished.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            lock.lock();
            pool->busy = false;
            pool->doneCv.notify_one();
        } else {
            lock.lock();
        }
    }
}

void ShutdownAtExit() {
    Shutdown();
}

}

void Init() {
    if (g_pool) {
        return;
    }

    Pool* pool = new Pool();
    pool->owner = std::this_thread::get_id();
    uint32_t workers = ChooseWorkers();
    pool->threads.reserve(workers);

    for (uint32_t i = 0; i < workers; ++i) {
        pool->threads.emplace_back(WorkerLoop, pool);
    }

    g_pool = pool;
    std::atexit(ShutdownAtExit);
}

void Shutdown() {
    Pool* pool = g_pool;

    if (!pool) {
        return;
    }

    g_pool = nullptr;

    {
        std::lock_guard<std::mutex> lock(pool->mutex);
        pool->stop = true;
    }

    pool->workCv.notify_all();

    for (std::thread& thread : pool->threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    delete pool;
}

uint32_t WorkerCount() {
    if (!g_pool) {
        return 0;
    }

    return static_cast<uint32_t>(g_pool->threads.size());
}

void ParallelFor(uint32_t count, RangeFn fn, void* user) {
    if (!count || !fn) {
        return;
    }

    Pool* pool = g_pool;

    if (!pool || std::this_thread::get_id() != pool->owner || pool->threads.empty()) {
        fn(Range{0, count}, user);
        return;
    }

    uint32_t workers = static_cast<uint32_t>(pool->threads.size());
    uint32_t chunks = workers + 1;

    if (chunks > count) {
        chunks = count;
    }

    {
        std::lock_guard<std::mutex> lock(pool->mutex);

        if (pool->busy) {
            chunks = 0;
        } else {
            // -- ʕ •ᴥ•ʔ✿ One chunk per worker, plus the chunk this thread runs. ✿ ʕ •ᴥ•ʔ
            pool->fn = fn;
            pool->user = user;
            pool->chunkSize = count / chunks;
            pool->remainder = count % chunks;
            pool->workerChunks = chunks - 1;
            pool->next.store(0, std::memory_order_relaxed);
            pool->unfinished.store(workers, std::memory_order_relaxed);
            pool->busy = true;
            pool->epoch += 1;
            pool->workCv.notify_all();
        }
    }

    if (!chunks) {
        fn(Range{0, count}, user);
        return;
    }

    fn(ChunkRange(*pool, chunks - 1), user);

    std::unique_lock<std::mutex> lock(pool->mutex);
    pool->doneCv.wait(lock, [&] { return !pool->busy; });
}

}
