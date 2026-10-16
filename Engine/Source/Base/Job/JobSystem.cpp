#include "Engine/Source/Base/Job/JobSystem.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include "Engine/Source/Base/OsPosix.h"
#endif

namespace Job {

namespace {

struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable done;
    std::vector<std::thread> threads;
    std::vector<Range> ranges;
    RangeFn fn = nullptr;
    void* user = nullptr;
    uint32_t workerCount = 0;
    uint32_t workerChunks = 0;
    uint32_t next = 0;
    uint32_t finished = 0;
    uint32_t generation = 0;
    bool stop = false;
    bool running = false;
};

State g_state;

void WorkerMain() {
#if defined(__linux__) || defined(__APPLE__)
    OsPosixSetThreadName("wow-job");
#endif

    std::unique_lock<std::mutex> lock(g_state.mutex);
    uint32_t seen = 0;

    while (!g_state.stop) {
        g_state.wake.wait(lock, [&] { return g_state.stop || g_state.generation != seen; });

        if (g_state.stop) {
            break;
        }

        seen = g_state.generation;

        while (g_state.next < g_state.workerChunks) {
            Range range = g_state.ranges[g_state.next++];
            RangeFn fn = g_state.fn;
            void* user = g_state.user;
            lock.unlock();
            fn(range, user);
            lock.lock();
            g_state.finished++;

            if (g_state.finished == g_state.workerChunks) {
                g_state.done.notify_one();
            }
        }
    }
}

}

void Init() {
    if (g_state.running) {
        return;
    }

    // -- ʕ •ᴥ•ʔ✿ Leave two cores for the main thread and the driver ✿ ʕ •ᴥ•ʔ
    uint32_t cores = std::thread::hardware_concurrency();
    uint32_t workers = 1;

    if (cores > 2) {
        workers = cores - 2;
    }

    g_state.stop = false;
    g_state.workerCount = workers;
    g_state.threads.reserve(workers);

    for (uint32_t i = 0; i < workers; i++) {
        g_state.threads.emplace_back(WorkerMain);
    }

    g_state.running = true;
}

void Shutdown() {
    std::vector<std::thread> threads;

    {
        std::lock_guard<std::mutex> lock(g_state.mutex);

        if (!g_state.running) {
            return;
        }

        g_state.stop = true;
        g_state.running = false;
        g_state.workerCount = 0;
        threads.swap(g_state.threads);
    }

    g_state.wake.notify_all();

    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::lock_guard<std::mutex> lock(g_state.mutex);
    g_state.stop = false;
    g_state.fn = nullptr;
    g_state.user = nullptr;
    g_state.ranges.clear();
}

uint32_t WorkerCount() {
    return g_state.workerCount;
}

void ParallelFor(uint32_t count, RangeFn fn, void* user) {
    if (count == 0 || !fn) {
        return;
    }

    if (!g_state.running) {
        Init();
    }

    // -- ʕ •ᴥ•ʔ✿ Main thread keeps one chunk; fn must not call Job:: ✿ ʕ •ᴥ•ʔ
    uint32_t chunks = g_state.workerCount + 1;

    if (chunks > count) {
        chunks = count;
    }

    if (chunks <= 1) {
        fn(Range{0, count}, user);
        return;
    }

    std::vector<Range> ranges(chunks);
    uint32_t base = count / chunks;
    uint32_t extra = count % chunks;
    uint32_t cursor = 0;

    for (uint32_t i = 0; i < chunks; i++) {
        uint32_t length = base + (i < extra ? 1u : 0u);
        ranges[i].begin = cursor;
        ranges[i].end = cursor + length;
        cursor += length;
    }

    Range mainRange = ranges[chunks - 1];
    uint32_t workerChunks = chunks - 1;

    {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        g_state.fn = fn;
        g_state.user = user;
        g_state.ranges.swap(ranges);
        g_state.workerChunks = workerChunks;
        g_state.next = 0;
        g_state.finished = 0;
        g_state.generation++;
    }

    g_state.wake.notify_all();
    fn(mainRange, user);

    std::unique_lock<std::mutex> lock(g_state.mutex);
    g_state.done.wait(lock, [&] { return g_state.finished == g_state.workerChunks; });
    g_state.fn = nullptr;
    g_state.user = nullptr;
}

}
