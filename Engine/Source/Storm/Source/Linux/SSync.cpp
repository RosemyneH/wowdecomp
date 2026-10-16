#if defined(__linux__) || defined(__APPLE__)

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include "Engine/Source/Storm/Source/Linux/SSync.h"

struct SSYNCHANDLE {
    SSYNCTYPE type;
    int32_t manualReset;
    int32_t count;
    int32_t maximum;
    pthread_t owner;
    std::atomic<int32_t> refCount;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
};

static pthread_mutex_t s_multiMutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t s_multiCond;

static pthread_once_t s_multiOnce = PTHREAD_ONCE_INIT;

static std::atomic<int32_t> s_multiWaiters;

static void InitCond(pthread_cond_t* cond) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#if !defined(__APPLE__)
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    pthread_cond_init(cond, &attr);
    pthread_condattr_destroy(&attr);
}

static void InitMulti() {
    InitCond(&s_multiCond);
}

static void Deadline(timespec* deadline, uint32_t timeout) {
#if defined(__APPLE__)
    clock_gettime(CLOCK_REALTIME, deadline);
#else
    clock_gettime(CLOCK_MONOTONIC, deadline);
#endif
    deadline->tv_sec += timeout / 1000;
    deadline->tv_nsec += static_cast<long>(timeout % 1000) * 1000000;

    if (deadline->tv_nsec >= 1000000000) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000;
    }
}

static int32_t TimedWait(pthread_cond_t* cond, pthread_mutex_t* mutex, uint32_t timeout, const timespec* deadline) {
    if (timeout == INFINITE) {
        pthread_cond_wait(cond, mutex);
        return 1;
    }

    return pthread_cond_timedwait(cond, mutex, deadline) != ETIMEDOUT;
}

static void NotifyMulti() {
    std::atomic_thread_fence(std::memory_order_seq_cst);

    if (s_multiWaiters.load()) {
        pthread_mutex_lock(&s_multiMutex);
        pthread_cond_broadcast(&s_multiCond);
        pthread_mutex_unlock(&s_multiMutex);
    }
}

static int32_t IsReady(SSYNCHANDLE* sync) {
    switch (sync->type) {
        case SSYNC_EVENT:
        case SSYNC_SEMAPHORE:
        case SSYNC_THREAD:
            return sync->count > 0;
        case SSYNC_MUTEX:
            return !sync->count || pthread_equal(sync->owner, pthread_self());
    }

    return 0;
}

static void Acquire(SSYNCHANDLE* sync) {
    switch (sync->type) {
        case SSYNC_EVENT:
            if (!sync->manualReset) {
                sync->count = 0;
            }

            break;
        case SSYNC_SEMAPHORE:
            sync->count--;
            break;
        case SSYNC_MUTEX:
            sync->count++;
            sync->owner = pthread_self();
            break;
        case SSYNC_THREAD:
            break;
    }
}

static int32_t TryAcquire(SSYNCHANDLE* sync) {
    if (!IsReady(sync)) {
        return 0;
    }

    Acquire(sync);
    return 1;
}

HANDLE SSyncCreate(SSYNCTYPE type, int32_t count, int32_t maximum, int32_t manualReset) {
    pthread_once(&s_multiOnce, InitMulti);

    SSYNCHANDLE* sync = new SSYNCHANDLE;
    sync->type = type;
    sync->manualReset = manualReset;
    sync->count = count;
    sync->maximum = maximum;
    sync->owner = pthread_self();
    sync->refCount = 1;
    pthread_mutex_init(&sync->mutex, nullptr);
    InitCond(&sync->cond);

    return sync;
}

void SSyncAddRef(HANDLE handle) {
    if (handle) static_cast<SSYNCHANDLE*>(handle)->refCount++;
}

void SSyncClose(HANDLE handle) {
    SSYNCHANDLE* sync = static_cast<SSYNCHANDLE*>(handle);
    if (!sync) return;

    if (--sync->refCount == 0) {
        pthread_cond_destroy(&sync->cond);
        pthread_mutex_destroy(&sync->mutex);
        delete sync;
    }
}

int32_t SSyncSignal(HANDLE handle, int32_t count) {
    SSYNCHANDLE* sync = static_cast<SSYNCHANDLE*>(handle);
    if (!sync) return 0;
    pthread_mutex_lock(&sync->mutex);

    if (sync->type == SSYNC_SEMAPHORE) {
        if (sync->count + count > sync->maximum) {
            pthread_mutex_unlock(&sync->mutex);
            return 0;
        }

        sync->count += count;
    } else {
        sync->count = 1;
    }

    pthread_cond_broadcast(&sync->cond);
    pthread_mutex_unlock(&sync->mutex);
    NotifyMulti();

    return 1;
}

int32_t SSyncReset(HANDLE handle) {
    SSYNCHANDLE* sync = static_cast<SSYNCHANDLE*>(handle);
    if (!sync) return 0;
    pthread_mutex_lock(&sync->mutex);
    sync->count = 0;
    pthread_mutex_unlock(&sync->mutex);

    return 1;
}

int32_t SSyncRelease(HANDLE handle) {
    SSYNCHANDLE* sync = static_cast<SSYNCHANDLE*>(handle);
    if (!sync) return 0;
    pthread_mutex_lock(&sync->mutex);

    if (!sync->count || !pthread_equal(sync->owner, pthread_self())) {
        pthread_mutex_unlock(&sync->mutex);
        return 0;
    }

    if (--sync->count == 0) {
        pthread_cond_broadcast(&sync->cond);
    }

    pthread_mutex_unlock(&sync->mutex);
    NotifyMulti();

    return 1;
}

uint32_t SSyncWait(HANDLE handle, uint32_t timeout) {
    SSYNCHANDLE* sync = static_cast<SSYNCHANDLE*>(handle);
    if (!sync) return WAIT_FAILED;
    timespec deadline;
    Deadline(&deadline, timeout);

    pthread_mutex_lock(&sync->mutex);

    while (!TryAcquire(sync)) {
        if (!timeout || !TimedWait(&sync->cond, &sync->mutex, timeout, &deadline)) {
            uint32_t result = TryAcquire(sync) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
            pthread_mutex_unlock(&sync->mutex);
            return result;
        }
    }

    pthread_mutex_unlock(&sync->mutex);

    return WAIT_OBJECT_0;
}

static int32_t TryAcquireAll(uint32_t count, SSYNCHANDLE** syncs) {
    for (uint32_t i = 0; i < count; i++) {
        pthread_mutex_lock(&syncs[i]->mutex);
    }

    int32_t ready = 1;

    for (uint32_t i = 0; i < count && ready; i++) {
        ready = IsReady(syncs[i]);
    }

    if (ready) {
        for (uint32_t i = 0; i < count; i++) {
            Acquire(syncs[i]);
        }
    }

    for (uint32_t i = count; i > 0; i--) {
        pthread_mutex_unlock(&syncs[i - 1]->mutex);
    }

    return ready;
}

static int32_t TryAcquireAny(uint32_t count, HANDLE* handles) {
    for (uint32_t i = 0; i < count; i++) {
        SSYNCHANDLE* sync = static_cast<SSYNCHANDLE*>(handles[i]);
        pthread_mutex_lock(&sync->mutex);
        int32_t acquired = TryAcquire(sync);
        pthread_mutex_unlock(&sync->mutex);

        if (acquired) {
            return static_cast<int32_t>(i);
        }
    }

    return -1;
}

uint32_t SSyncWaitMultiple(uint32_t count, HANDLE* handles, int32_t waitAll, uint32_t timeout) {
    if (!count) {
        return WAIT_FAILED;
    }

    SSYNCHANDLE* sorted[64];

    for (uint32_t i = 0; i < count; i++) {
        sorted[i] = static_cast<SSYNCHANDLE*>(handles[i]);
    }

    std::sort(sorted, sorted + count);

    timespec deadline;
    Deadline(&deadline, timeout);

    s_multiWaiters++;
    pthread_mutex_lock(&s_multiMutex);

    uint32_t result;

    for (;;) {
        if (waitAll) {
            if (TryAcquireAll(count, sorted)) {
                result = WAIT_OBJECT_0;
                break;
            }
        } else {
            int32_t index = TryAcquireAny(count, handles);

            if (index >= 0) {
                result = WAIT_OBJECT_0 + index;
                break;
            }
        }

        if (!timeout || !TimedWait(&s_multiCond, &s_multiMutex, timeout, &deadline)) {
            result = WAIT_TIMEOUT;
            break;
        }
    }

    pthread_mutex_unlock(&s_multiMutex);
    s_multiWaiters--;

    return result;
}

#endif
