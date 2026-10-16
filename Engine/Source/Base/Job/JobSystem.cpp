#include "Engine/Source/Base/Job/JobSystem.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace Job {

namespace {

struct Slot {
    uint32_t begin;
    uint32_t end;
};

std::mutex g_mu;
std::condition_variable g_wake;
std::condition_variable g_done;
std::vector<std::thread> g_threads;
std::vector<Slot> g_slots;
std::atomic<uint32_t> g_remaining{0};
std::atomic<uint32_t> g_epoch{0};
RangeFn g_fn = nullptr;
void* g_user = nullptr;
bool g_stop = false;
bool g_inited = false;
uint32_t g_workerCount = 1;

void WorkerLoop(uint32_t index) {
    uint32_t seen = 0;

    for (;;) {
        Range range{0, 0};
        RangeFn fn = nullptr;
        void* user = nullptr;

        {
            std::unique_lock<std::mutex> lock(g_mu);
            g_wake.wait(lock, [&] { return g_stop || g_epoch.load(std::memory_order_relaxed) != seen; });

            if (g_stop) {
                return;
            }

            seen = g_epoch.load(std::memory_order_relaxed);
            range = Range{g_slots[index].begin, g_slots[index].end};
            fn = g_fn;
            user = g_user;
        }

        if (range.begin < range.end && fn) {
            fn(range, user);
        }

        if (range.begin < range.end && g_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> lock(g_mu);
            g_done.notify_one();
        }
    }
}

struct ShutdownGuard {
    ~ShutdownGuard() {
        Shutdown();
    }
};

}

void Init() {
    std::lock_guard<std::mutex> lock(g_mu);

    if (g_inited) {
        return;
    }

    // -- ʕ •ᴥ•ʔ✿ Leave two cores for the rest of the frame ✿ ʕ •ᴥ•ʔ
    uint32_t hw = std::thread::hardware_concurrency();
    g_workerCount = hw > 2 ? hw - 2 : 1;
    g_slots.assign(g_workerCount, Slot{0, 0});
    g_stop = false;

    for (uint32_t i = 1; i < g_workerCount; ++i) {
        g_threads.emplace_back(WorkerLoop, i);
    }

    g_inited = true;
    static ShutdownGuard guard;
}

void Shutdown() {
    std::vector<std::thread> threads;

    {
        std::lock_guard<std::mutex> lock(g_mu);

        if (!g_inited) {
            return;
        }

        g_stop = true;
        g_inited = false;
        threads.swap(g_threads);
        g_workerCount = 1;
    }

    g_wake.notify_all();

    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    g_stop = false;
}

uint32_t WorkerCount() {
    Init();
    return g_workerCount;
}

void ParallelFor(uint32_t count, RangeFn fn, void* user) {
    if (!count || !fn) {
        return;
    }

    Init();

    uint32_t workers = g_workerCount;

    if (workers > count) {
        workers = count;
    }

    uint32_t base = count / workers;
    uint32_t extra = count % workers;
    uint32_t cursor = 0;
    Range mainRange{0, 0};

    {
        std::lock_guard<std::mutex> lock(g_mu);

        for (uint32_t i = 0; i < g_workerCount; ++i) {
            if (i < workers) {
                uint32_t len = base + (i < extra ? 1u : 0u);
                g_slots[i].begin = cursor;
                g_slots[i].end = cursor + len;
                cursor += len;
            } else {
                g_slots[i].begin = 0;
                g_slots[i].end = 0;
            }
        }

        g_fn = fn;
        g_user = user;
        mainRange = Range{g_slots[0].begin, g_slots[0].end};
        g_remaining.store(workers, std::memory_order_relaxed);
        g_epoch.fetch_add(1, std::memory_order_relaxed);
    }

    g_wake.notify_all();

    fn(mainRange, user);

    if (g_remaining.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        std::unique_lock<std::mutex> lock(g_mu);
        g_done.wait(lock, [] { return g_remaining.load(std::memory_order_acquire) == 0; });
    }
}

}
