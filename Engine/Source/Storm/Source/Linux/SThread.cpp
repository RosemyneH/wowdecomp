#if defined(__linux__) || defined(__APPLE__)

#include "Engine/Source/Base/OsPosix.h"
#include <cstdint>
#include <cstring>
#include <limits.h>
#include <new>
#include <pthread.h>
#include <sched.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include "Engine/Source/Storm/Source/Linux/SSync.h"
#include "Engine/Source/Storm/Source/SThread.h"
#include "Engine/Source/Storm/H/storm.h"

extern char** environ;

struct PROCESSCOMPLETION {
    void (*callback)(void*);
    void* data;
    pid_t process;
};

struct THREADLAUNCH {
    uint32_t (STORMAPI* threadProc)(void*);
    void* param;
    HANDLE threadH;
    HANDLE started;
    uint32_t threadId;
};

namespace S_Thread {
    SThreadTrack s_threads[s_maxthreads];

    int32_t s_numthreads;

    CInitCritSect s_threadCrit;
}

uint32_t STORMAPI SGetCurrentThreadId() {
    return OsPosixThreadId();
}

uint32_t STORMAPI SGetCurrentProcessId() {
    return static_cast<uint32_t>(getpid());
}

int32_t STORMAPI SGetCurrentThreadPriority() {
    return 0;
}

int32_t STORMAPI SSetCurrentThreadPriority(int32_t priority) {
    return 1;
}

int32_t STORMAPI SSetCurrentProcessAffinityMask(size_t mask) {
#if defined(__APPLE__)
    (void)mask;
    return 1;
#else
    cpu_set_t set;
    CPU_ZERO(&set);

    for (uint32_t cpu = 0; cpu < sizeof(mask) * 8; cpu++) {
        if (mask & (static_cast<size_t>(1) << cpu)) {
            CPU_SET(cpu, &set);
        }
    }

    return sched_setaffinity(0, sizeof(set), &set) == 0;
#endif
}

static void SetThreadName(const char* threadName) {
    char name[16];
    SStrCopy(name, threadName ? threadName : "", sizeof(name));
    OsPosixSetThreadName(name);
}

int32_t CInitCritSect::Enter() {
    int32_t result = 0;
    CCritSect* critsect = __atomic_load_n(&this->m_critsect, __ATOMIC_ACQUIRE);

    if (!critsect) {
        while (__atomic_exchange_n(&this->m_spinLock, 1, __ATOMIC_ACQUIRE)) {
        }

        critsect = __atomic_load_n(&this->m_critsect, __ATOMIC_RELAXED);

        if (!critsect) {
            critsect = new (this->m_critsectData) CCritSect();
            __atomic_store_n(&this->m_critsect, critsect, __ATOMIC_RELEASE);
            result = 1;
        }

        __atomic_store_n(&this->m_spinLock, 0, __ATOMIC_RELEASE);
    }

    critsect->Enter();
    return result;
}

void CInitCritSect::Leave() {
    CCritSect* critsect = __atomic_load_n(&this->m_critsect, __ATOMIC_ACQUIRE);
    critsect->Leave();
}

static uint32_t STORMAPI ProcessCompletionCallbackThread(void* vdata) {
    PROCESSCOMPLETION* completion = static_cast<PROCESSCOMPLETION*>(vdata);
    int status;
    waitpid(completion->process, &status, 0);
    completion->callback(completion->data);
    SMemFree(completion, __FILE__, __LINE__, 0);
    return 0;
}

uint32_t STORMAPI S_Thread::s_SLaunchThread(void* threadParam) {
    s_threadCrit.Enter();

    THREADLAUNCH* launch = static_cast<THREADLAUNCH*>(threadParam);
    uint32_t (STORMAPI* threadProc)(void*) = launch->threadProc;
    void* param = launch->param;
    uint32_t threadId = SGetCurrentThreadId();
    const char* threadName = nullptr;

    for (int32_t i = 0; i < s_numthreads; i++) {
        if (s_threads[i].threadId == threadId) {
            threadName = s_threads[i].name;
            break;
        }
    }

    SMemFree(launch, __FILE__, __LINE__, 0);
    SetThreadName(threadName);
    s_threadCrit.Leave();

    uint32_t threadVal = threadProc(param);

    s_threadCrit.Enter();

    int32_t numthreads = s_numthreads;

    for (int32_t i = 0; i < numthreads; i++) {
        if (s_threads[i].live && s_threads[i].threadId == threadId) {
            memmove(&s_threads[i], &s_threads[i + 1], (s_maxthreads - 1 - i) * sizeof(SThreadTrack));
            numthreads--;
        }
    }

    s_numthreads = numthreads;
    s_threadCrit.Leave();

    return threadVal;
}

static void* ThreadEntry(void* param) {
    THREADLAUNCH* launch = static_cast<THREADLAUNCH*>(param);
    HANDLE threadH = launch->threadH;
    HANDLE started = launch->started;

    launch->threadId = SGetCurrentThreadId();
    SSyncSignal(started, 1);
    SSyncClose(started);

    S_Thread::s_SLaunchThread(launch);

    SSyncSignal(threadH, 1);
    SSyncClose(threadH);

    return nullptr;
}

HANDLE SCreateThread(uint32_t dwStackSize, uint32_t (STORMAPI* lpStartAddress)(void*), void* lpParameter, uint32_t dwCreationFlags, uint32_t* lpThreadId, char* threadName) {
    if (!threadName) {
        threadName = const_cast<char*>("");
    }

    S_Thread::s_threadCrit.Enter();

    if (!S_Thread::s_numthreads) {
        S_Thread::SThreadTrack* track = &S_Thread::s_threads[S_Thread::s_numthreads];
        track->threadId = SGetCurrentThreadId();
        track->threadH = nullptr;
        track->live = 1;
        track->suspended = 0;
        SStrCopy(track->name, "main", sizeof(track->name));
        S_Thread::s_numthreads++;
    }

    S_Thread::s_threadCrit.Leave();

    THREADLAUNCH* launch = static_cast<THREADLAUNCH*>(SMemAlloc(sizeof(THREADLAUNCH), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    launch->threadProc = lpStartAddress;
    launch->param = lpParameter;
    launch->threadH = SSyncCreate(SSYNC_THREAD, 0, 1, 1);
    launch->started = SSyncCreate(SSYNC_EVENT, 0, 1, 1);

    HANDLE hThread = launch->threadH;
    HANDLE started = launch->started;
    SSyncAddRef(hThread);
    SSyncAddRef(started);

    S_Thread::s_threadCrit.Enter();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (dwStackSize) {
        pthread_attr_setstacksize(&attr, dwStackSize < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : dwStackSize);
    }

    pthread_t thread;
    int result = pthread_create(&thread, &attr, ThreadEntry, launch);
    pthread_attr_destroy(&attr);

    if (result) {
        S_Thread::s_threadCrit.Leave();
        SSyncClose(started);
        SSyncClose(started);
        SSyncClose(hThread);
        SSyncClose(hThread);
        SMemFree(launch, __FILE__, __LINE__, 0);
        return nullptr;
    }

    SSyncWait(started, INFINITE);
    SSyncClose(started);
    *lpThreadId = launch->threadId;

    S_Thread::SThreadTrack* track = &S_Thread::s_threads[S_Thread::s_numthreads];
    track->threadId = *lpThreadId;
    track->threadH = hThread;
    track->live = 1;
    track->suspended = 0;
    SStrCopy(track->name, threadName, sizeof(track->name));

    S_Thread::s_numthreads++;
    S_Thread::s_threadCrit.Leave();

    return hThread;
}

int32_t STORMAPI SGetThreadName(uint32_t threadId, char* buffer, int32_t bufferchars) {
    bool found = false;

    if (!buffer || bufferchars < 1) {
        return 0;
    }

    if (bufferchars > static_cast<int32_t>(sizeof(S_Thread::s_threads[0].name))) {
        bufferchars = sizeof(S_Thread::s_threads[0].name);
    }

    *buffer = 0;

    S_Thread::s_threadCrit.Enter();

    for (int32_t i = 0; i < S_Thread::s_numthreads; i++) {
        if (S_Thread::s_threads[i].threadId == threadId) {
            SStrCopy(buffer, S_Thread::s_threads[i].name, bufferchars);
            found = true;
            break;
        }
    }

    S_Thread::s_threadCrit.Leave();

    return found;
}

HANDLE STORMAPI SCreateThread(uint32_t (STORMAPI* lpStartAddress)(void*), void* lpParameter, uint32_t* lpThreadId, void* linuxData, char* threadName, uint32_t stackSize) {
    return SCreateThread(stackSize, lpStartAddress, lpParameter, 0, lpThreadId, threadName);
}

int32_t STORMAPI SCreateProcess(const char* appName, char* commandLine, void (*callbackWhenProcessCompletes)(void*), void* callbackData) {
    const char* command = commandLine && *commandLine ? commandLine : appName;

    if (!command) {
        return 0;
    }

    char shell[] = "/bin/sh";
    char flag[] = "-c";
    char* argv[] = { shell, flag, const_cast<char*>(command), nullptr };

    pid_t process;

    if (posix_spawn(&process, shell, nullptr, nullptr, argv, environ)) {
        return 0;
    }

    if (callbackWhenProcessCompletes) {
        PROCESSCOMPLETION* completion = static_cast<PROCESSCOMPLETION*>(SMemAlloc(sizeof(PROCESSCOMPLETION), __FILE__, __LINE__, 0));
        completion->data = callbackData;
        completion->callback = callbackWhenProcessCompletes;
        completion->process = process;

        uint32_t threadId;
        SCreateThread(ProcessCompletionCallbackThread, completion, &threadId, nullptr, nullptr, 0);
    }

    return 1;
}

#endif
