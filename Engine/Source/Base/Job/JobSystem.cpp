#include "Engine/Source/Base/Job/JobSystem.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#if defined(__linux__) || defined(__APPLE__)
#include "Engine/Source/Base/OsPosix.h"
#endif

namespace Job {
namespace {

struct Slot {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    RangeFn fn = nullptr;
    void* user = nullptr;
    Range range{};
    uint32_t epoch = 0;
    bool stop = false;
};

struct Pool {
    std::mutex mutex;
    std::unique_ptr<Slot[]> slots;
    uint32_t threads = 0;
    uint32_t chunks = 0;
    uint32_t refs = 0;
    std::thread::id main{};

    ~Pool() {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->StopUnlocked();
    }

    void StopUnlocked() {
        if (!this->slots) {
            return;
        }

        for (uint32_t i = 0; i < this->threads; i++) {
            std::lock_guard<std::mutex> lock(this->slots[i].mutex);
            this->slots[i].stop = true;
        }

        for (uint32_t i = 0; i < this->threads; i++) {
            this->slots[i].cv.notify_one();
        }

        for (uint32_t i = 0; i < this->threads; i++) {
            if (this->slots[i].thread.joinable()) {
                this->slots[i].thread.join();
            }
        }

        this->slots.reset();
        this->threads = 0;
        this->chunks = 0;
        this->refs = 0;
    }
};

Pool& State() {
    static Pool pool;
    return pool;
}

// -- ʕ •ᴥ•ʔ✿ Leave two cores free. One chunk is the caller, so the pool is never empty ✿ ʕ •ᴥ•ʔ
uint32_t ChunkCount() {
    uint32_t cores = std::thread::hardware_concurrency();

    if (cores > 3) {
        return cores - 2;
    }

    return 2;
}

Range ChunkRange(uint32_t chunk, uint32_t chunks, uint32_t count) {
    uint32_t base = count / chunks;
    uint32_t extra = count % chunks;
    uint32_t begin = chunk * base + (chunk < extra ? chunk : extra);
    uint32_t len = base + (chunk < extra ? 1u : 0u);
    return Range{begin, begin + len};
}

void WorkerMain(Slot* slot) {
#if defined(__linux__) || defined(__APPLE__)
    OsPosixSetThreadName("wow-job");
#endif

    uint32_t seen = 0;

    for (;;) {
        RangeFn fn = nullptr;
        void* user = nullptr;
        Range range{};

        {
            std::unique_lock<std::mutex> lock(slot->mutex);
            slot->cv.wait(lock, [&] { return slot->stop || slot->epoch != seen; });

            if (slot->stop) {
                return;
            }

            seen = slot->epoch;
            fn = slot->fn;
            user = slot->user;
            range = slot->range;
        }

        if (fn && range.begin < range.end) {
            fn(range, user);
        }

        {
            std::lock_guard<std::mutex> lock(slot->mutex);
            slot->fn = nullptr;
        }

        slot->cv.notify_one();
    }
}

}

void Init() {
    Pool& pool = State();
    std::lock_guard<std::mutex> lock(pool.mutex);

    if (pool.refs++ > 0) {
        return;
    }

    pool.main = std::this_thread::get_id();
    pool.chunks = ChunkCount();
    pool.threads = pool.chunks - 1;
    pool.slots = std::make_unique<Slot[]>(pool.threads);

    for (uint32_t i = 0; i < pool.threads; i++) {
        pool.slots[i].thread = std::thread(WorkerMain, &pool.slots[i]);
    }
}

void Shutdown() {
    Pool& pool = State();
    std::lock_guard<std::mutex> lock(pool.mutex);

    if (pool.refs == 0) {
        return;
    }

    pool.refs -= 1;

    if (pool.refs > 0) {
        return;
    }

    pool.StopUnlocked();
}

uint32_t WorkerCount() {
    Pool& pool = State();
    std::lock_guard<std::mutex> lock(pool.mutex);
    return pool.refs ? pool.chunks : 0;
}

void ParallelFor(uint32_t count, RangeFn fn, void* user) {
    if (!fn || count == 0) {
        return;
    }

    static thread_local uint32_t depth = 0;

    if (depth != 0) {
        fn(Range{0, count}, user);
        return;
    }

    Pool& pool = State();
    Slot* slots = nullptr;
    uint32_t threads = 0;
    uint32_t chunks = 0;
    std::thread::id main{};

    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        slots = pool.slots.get();
        threads = pool.threads;
        chunks = pool.chunks;
        main = pool.main;
    }

    if (!slots || chunks < 2 || std::this_thread::get_id() != main) {
        fn(Range{0, count}, user);
        return;
    }

    depth = 1;

    for (uint32_t i = 0; i < threads; i++) {
        Range range = ChunkRange(i + 1, chunks, count);
        Slot& slot = slots[i];

        {
            std::lock_guard<std::mutex> lock(slot.mutex);
            slot.fn = fn;
            slot.user = user;
            slot.range = range;
            slot.epoch += 1;
        }

        slot.cv.notify_one();
    }

    Range mine = ChunkRange(0, chunks, count);

    if (mine.begin < mine.end) {
        fn(mine, user);
    }

    for (uint32_t i = 0; i < threads; i++) {
        Slot& slot = slots[i];
        std::unique_lock<std::mutex> lock(slot.mutex);
        slot.cv.wait(lock, [&] { return slot.fn == nullptr; });
    }

    depth = 0;
}

}
