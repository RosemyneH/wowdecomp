#if defined(__linux__) || defined(__APPLE__)

#include <cstring>
#include <ctime>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/Linux/SSync.h"
#include "Engine/Source/Storm/Source/SLock.h"
#include "Engine/Source/Storm/Source/SThread.h"
#include "Engine/Source/Storm/H/storm.h"

#if defined(__x86_64__) || defined(__i386__)
#define SLOCK_SPIN_PAUSE() __builtin_ia32_pause()
#else
#define SLOCK_SPIN_PAUSE() __asm__ __volatile__("" ::: "memory")
#endif

static uint32_t s_spinCount = 1;

static volatile LONG s_eventCount[2];

static volatile LONG s_freeIndex[2];

static volatile LONG s_allocIndex[2];

static volatile LONG s_eventIds[2][2048];

static HANDLE s_events[2][1024];

static int32_t s_refCount;

static CInitCritSect s_refCritsect;

template <class T>
CDebugLockEntry TDebugLock<T>::s_entries[256];

template <class T>
uint32_t TDebugLock<T>::s_freeEntry;

template <class T>
CInitCritSect TDebugLock<T>::s_critsect;

template <class T>
CDebugLock* TDebugLock<T>::s_lockList;

static volatile LONG s_stallCount;

static LONG InterlockedExchange(volatile LONG* target, LONG value) {
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static LONG InterlockedCompareExchange(volatile LONG* target, LONG value, LONG comparand) {
    __atomic_compare_exchange_n(target, &comparand, value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}

static uint32_t GetTickCount() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint32_t>(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

void SRWLock::IInitialize() {
    for (uint32_t type = 0; type < 2; type++) {
        for (uint32_t i = 0; i < 1024; i++) {
            s_eventIds[type][i] = (i + 1) << 21;
            s_events[type][i] = SSyncCreate(SSYNC_EVENT, 0, 1, type == 1);
        }

        s_allocIndex[type] = 2047;
        s_freeIndex[type] = 1023;
        s_eventCount[type] = 1024;
    }

    s_spinCount = sysconf(_SC_NPROCESSORS_ONLN) > 1 ? 4000 : 1;

    SServerInitialize();
}

void SRWLock::IDestroy() {
    for (uint32_t type = 0; type < 2; type++) {
        for (uint32_t i = 0; i < 1024; i++) {
            SSyncClose(s_events[type][i]);
            s_events[type][i] = nullptr;
            s_eventIds[type][i] = 0;
        }

        s_allocIndex[type] = 0;
        s_freeIndex[type] = 0;
        s_eventCount[type] = 0;
    }

    SServerDestroy();
}

uint32_t SRWLock::IAllocEvent(uint32_t type) {
    if (__atomic_sub_fetch(&s_eventCount[type], 1, __ATOMIC_SEQ_CST) < 0) {
        DebugLog("IAllocEvent: too many %s event allocs", type ? "SUMREVTYPE" : "SUAREVTYPE");
    }

    volatile LONG* slot = &s_eventIds[type][__atomic_add_fetch(&s_allocIndex[type], 1, __ATOMIC_SEQ_CST) & 0x7FF];
    uint32_t event = InterlockedExchange(slot, 0);

    while (!event) {
        uint32_t spin = s_spinCount;

        while (true) {
            event = InterlockedExchange(slot, 0);

            if (event) {
                return event;
            }

            SLOCK_SPIN_PAUSE();

            if (!--spin) {
                break;
            }
        }

        sched_yield();
    }

    return event;
}

void SRWLock::IFreeEvent(uint32_t event, uint32_t type, int32_t reset) {
    if (__atomic_add_fetch(&s_eventCount[type], 1, __ATOMIC_SEQ_CST) > 1024) {
        DebugLog("IFreeEvent: too many %s event frees", type ? "SUMREVTYPE" : "SUAREVTYPE");
    }

    if (reset) {
        SSyncReset(s_events[type][(event >> 21) - 1]);
    }

    volatile LONG* slot = &s_eventIds[type][__atomic_add_fetch(&s_freeIndex[type], 1, __ATOMIC_SEQ_CST) & 0x7FF];
    LONG value = InterlockedExchange(slot, event & 0xFFE00000);

    while (value) {
        uint32_t spin = s_spinCount;

        while (true) {
            value = InterlockedExchange(slot, value);

            if (!value) {
                return;
            }

            SLOCK_SPIN_PAUSE();

            if (!--spin) {
                break;
            }
        }

        sched_yield();
    }
}

uint32_t SRWLock::IAddWaiter(volatile LONG* waiters, uint32_t type, uint32_t increment) {
    uint32_t event = IAllocEvent(type);
    LONG value = InterlockedCompareExchange(waiters, event, 0);

    while (value) {
        if (InterlockedCompareExchange(waiters, value + increment, value) == value) {
            IFreeEvent(event, type, 0);
            return value + increment;
        }

        value = InterlockedCompareExchange(waiters, event, 0);
    }

    return event;
}

int32_t SRWLock::IRemoveWaiter(volatile LONG* waiters, uint32_t expected, uint32_t type, uint32_t decrement) {
    LONG value = InterlockedCompareExchange(waiters, 0, expected);

    while (value != static_cast<LONG>(expected)) {
        if (InterlockedCompareExchange(waiters, value - decrement, value) == value) {
            return 0;
        }

        value = InterlockedCompareExchange(waiters, 0, expected);
    }

    IFreeEvent(expected, type, 1);
    return 1;
}

inline void SRWLock::SUNNLockLeave(volatile SUNNLOCK* sunnlock) {
    __atomic_store_n(&sunnlock->m_state, 1, __ATOMIC_SEQ_CST);

    uint32_t event = sunnlock->m_event;

    if (event) {
        SSyncSignal(s_events[0][(event >> 21) - 1], 1);
    }
}

void SRWLock::SURWLockLeave(volatile SURWLOCK* surwlock, int32_t fromwriting) {
    if (fromwriting) {
        SUNNLockLeave(&surwlock->m_mutex);
    } else if (IRemoveWaiter(&surwlock->m_readerEvent, (surwlock->m_readerEvent & 0xFFE00000) + 1, 1, 2)) {
        SUNNLockLeave(&surwlock->m_mutex);
    }
}

SCritSect::SCritSect() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&this->m_critsect, &attr);
    pthread_mutexattr_destroy(&attr);
}

SCritSect::~SCritSect() {
    pthread_mutex_destroy(&this->m_critsect);
}

void SCritSect::Enter() {
    pthread_mutex_lock(&this->m_critsect);
}

void SCritSect::Leave() {
    pthread_mutex_unlock(&this->m_critsect);
}

SSyncObject::SSyncObject() {
    this->m_opaqueData = nullptr;
}

void CSRWLock::Leave(int32_t fromwriting) {
    SRWLock::SURWLockLeave(&this->m_opaqueData, fromwriting);
}

int32_t SSyncObject::Valid() {
    return this->m_opaqueData != nullptr;
}

uint32_t SSyncObject::Wait(uint32_t timeout) {
    return SSyncWait(this->m_opaqueData, timeout);
}

uint32_t STORMAPI SSyncObject::WaitMultiplePtr(uint32_t count, SSyncObject** objects, int32_t waitAll, uint32_t timeout) {
    HANDLE handles[64];

    if (count > 64) {
        return 0xFFFFFFFF;
    }

    uint32_t handleCount = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (objects[i] && objects[i]->m_opaqueData) {
            handles[handleCount++] = objects[i]->m_opaqueData;
        }
    }

    return SSyncWaitMultiple(handleCount, handles, waitAll, timeout);
}

int32_t SEvent::Set() {
    return SSyncSignal(this->m_opaqueData, 1);
}

int32_t SEvent::Reset() {
    return SSyncReset(this->m_opaqueData);
}

int32_t SThread::Create(uint32_t (STORMAPI* threadProc)(void*), void* param, SThread& thread, char* threadName, uint32_t stackSize) {
    uint32_t threadId;
    thread.m_opaqueData = SCreateThread(threadProc, param, &threadId, nullptr, nullptr, stackSize);
    return thread.m_opaqueData != nullptr;
}

int32_t SThread::SetPriority(int32_t priority) {
    return priority < 6;
}

int32_t SSemaphore::Signal(uint32_t count) {
    return SSyncSignal(this->m_opaqueData, count);
}

void SMutex::Create(int32_t initialOwner, const char* name) {
    if (this->m_opaqueData) {
        SSyncClose(this->m_opaqueData);
        this->m_opaqueData = nullptr;
    }

    this->m_opaqueData = SSyncCreate(SSYNC_MUTEX, initialOwner ? 1 : 0, 0, 0);
}

int32_t SMutex::Release() {
    return SSyncRelease(this->m_opaqueData);
}

inline void SRWLock::IOsRWLockIncRef() {
    s_refCritsect.Enter();

    if (s_refCount++ == 0) {
        IInitialize();
    }

    s_refCritsect.Leave();
}

inline void SRWLock::IOsRWLockDecRef() {
    s_refCritsect.Enter();

    if (--s_refCount == 0) {
        IDestroy();
    }

    s_refCritsect.Leave();
}

inline void SRWLock::SURWLockInitialize(volatile SURWLOCK* surwlock) {
    surwlock->m_mutex.m_state = 1;
    surwlock->m_mutex.m_event = 0;
    surwlock->m_readerEvent = 0;
}

inline void SRWLock::SURWLockDelete(volatile SURWLOCK* surwlock) {
    surwlock->m_mutex.m_state = 0;

    uint32_t event = InterlockedExchange(&surwlock->m_mutex.m_event, 0);

    if (event) {
        IFreeEvent(event, 0, 1);
    }

    event = InterlockedExchange(&surwlock->m_readerEvent, 0);

    if (event) {
        IFreeEvent(event, 1, 1);
    }
}

CSRWLock::CSRWLock() {
    SRWLock::IOsRWLockIncRef();
    SRWLock::SURWLockInitialize(&this->m_opaqueData);
}

CSRWLock::~CSRWLock() {
    SRWLock::SURWLockDelete(&this->m_opaqueData);
    SRWLock::IOsRWLockDecRef();
}

SSyncObject::~SSyncObject() {
    if (this->m_opaqueData) {
        SSyncClose(this->m_opaqueData);
        this->m_opaqueData = nullptr;
    }
}

void SSyncObject::Close() {
    if (this->m_opaqueData) {
        SSyncClose(this->m_opaqueData);
        this->m_opaqueData = nullptr;
    }
}

SEvent::SEvent(int32_t manualReset, int32_t initialState) {
    this->m_opaqueData = SSyncCreate(SSYNC_EVENT, initialState ? 1 : 0, 1, manualReset);
}

SSemaphore::SSemaphore(int32_t initialCount, int32_t maximumCount) {
    this->m_opaqueData = SSyncCreate(SSYNC_SEMAPHORE, initialCount, maximumCount, 0);
}

SMutex::SMutex() {
    this->Create(0, nullptr);
}

template <class T>
void TDebugLock<T>::LockEntries() {
    if (s_critsect.Enter()) {
        memset(s_entries, 0, sizeof(s_entries));

        for (uint32_t i = 1; i < 255; i++) {
            s_entries[i].m_next = i + 1;
        }

        s_freeEntry = 1;
    }
}

template <class T>
void TDebugLock<T>::DumpEntries(CDebugLock* lock) {
    CDebugLockEntry* entry = nullptr;
    uint32_t now = GetTickCount();
    uint32_t index = lock->m_entry;

    while (index) {
        if (index >= 256) {
            SDebugPrintf("%s(%u) : CDebugLock:%p: entry has bad next %u\n", __FILE__, __LINE__, lock, index);

            if (entry) {
                entry->m_next = 0;
            } else {
                lock->m_entry = 0;
            }

            return;
        }

        entry = &s_entries[index];

        char threadName[16];
        SGetThreadName(entry->m_threadId, threadName, sizeof(threadName));

        SDebugPrintf("%s(%u) : CDebugLock:%p: tid:%03x %s %c %c t:%u\n", entry->m_file, entry->m_line & 0x3FFFFFFF, lock, entry->m_threadId, threadName, (entry->m_line & 0x40000000) ? 'W' : 'R', (entry->m_line & 0x80000000) ? 'T' : 'F', now - entry->m_time);

        index = entry->m_next;
    }
}

void CDebugSCritSect::DumpAllEntries() {
    LockEntries();

    SDebugPrintf("%s(%u) : CDebugSCritSect:DumpAllEntries\n", __FILE__, __LINE__);

    for (CDebugLock* lock = s_lockList; lock; lock = lock->m_next) {
        DumpEntries(lock);
    }

    s_critsect.Leave();
}

void CDebugSRWLock::DumpAllEntries() {
    LockEntries();

    SDebugPrintf("%s(%u) : CDebugSRWLock:DumpAllEntries\n", __FILE__, __LINE__);

    for (CDebugLock* lock = s_lockList; lock; lock = lock->m_next) {
        DumpEntries(lock);
    }

    s_critsect.Leave();
}

void SRWLock::SUNNLockEnter(volatile SUNNLOCK* sunnlock) {
    if (!sunnlock->m_event) {
        uint32_t spin = s_spinCount;

        while (true) {
            if (InterlockedExchange(&sunnlock->m_state, 0)) {
                return;
            }

            SLOCK_SPIN_PAUSE();

            if (!--spin || sunnlock->m_event) {
                break;
            }
        }
    }

    uint32_t event = IAddWaiter(&sunnlock->m_event, 0, 1) & 0xFFE00000;

    while (true) {
        uint32_t spin = s_spinCount;

        while (true) {
            if (InterlockedExchange(&sunnlock->m_state, 0)) {
                IRemoveWaiter(&sunnlock->m_event, event, 0, 1);
                return;
            }

            SLOCK_SPIN_PAUSE();

            if (!--spin) {
                break;
            }
        }

        if (SSyncWait(s_events[0][(static_cast<uint32_t>(sunnlock->m_event) >> 21) - 1], 60000)) {
            __atomic_add_fetch(&s_stallCount, 1, __ATOMIC_SEQ_CST);
            CDebugSCritSect::DumpAllEntries();
            CDebugSRWLock::DumpAllEntries();
        }
    }
}

void SRWLock::SURWLockEnter(volatile SURWLOCK* surwlock, int32_t forwriting) {
    if (forwriting) {
        SUNNLockEnter(&surwlock->m_mutex);
        return;
    }

    uint32_t event = IAddWaiter(&surwlock->m_readerEvent, 1, 2);

    if (event == (event & 0xFFE00000)) {
        SUNNLockEnter(&surwlock->m_mutex);
        __atomic_add_fetch(&surwlock->m_readerEvent, 1, __ATOMIC_SEQ_CST);

        if (event) {
            SSyncSignal(s_events[1][(event >> 21) - 1], 1);
        }

        return;
    }

    while (true) {
        uint32_t spin = s_spinCount;

        while (true) {
            if (surwlock->m_readerEvent & 1) {
                return;
            }

            SLOCK_SPIN_PAUSE();

            if (!--spin) {
                break;
            }
        }

        if (!SSyncWait(s_events[1][(event >> 21) - 1], 60000)) {
            return;
        }

        __atomic_add_fetch(&s_stallCount, 1, __ATOMIC_SEQ_CST);
        CDebugSCritSect::DumpAllEntries();
        CDebugSRWLock::DumpAllEntries();
    }
}

void CSRWLock::Enter(int32_t forwriting) {
    SRWLock::SURWLockEnter(&this->m_opaqueData, forwriting);
}

#endif
