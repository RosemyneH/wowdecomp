#if defined(__linux__) || defined(__APPLE__)

#include "Engine/Source/Base/OsPosix.h"
#include <cerrno>
#include <climits>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <execinfo.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/ThreadLocalStorage.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Linux/ThreadHandle.h"

bool System_Thread::s_initialized;

Blizzard::Lock::Mutex System_Thread::s_mutex;

Blizzard::Lock::Mutex System_Thread::s_registryMutex;

Blizzard::Thread::TLSSlot System_Thread::s_threadRecordTLS;

Blizzard::Thread::TLSSlot System_Thread::s_stackTraceEntryPointTLS;

Blizzard::Thread::ThreadRecord* System_Thread::s_mainThread;

std::set<Blizzard::Thread::ThreadRecord*, std::less<Blizzard::Thread::ThreadRecord*>, Blizzard::Memory::AllocSTL_raw<Blizzard::Thread::ThreadRecord*>>* System_Thread::s_ThreadRegistry;

int32_t System_Thread::s_DefaultStackSize;

System_Thread::ExternalTLSRecord System_Thread::s_externalTLS[32];

int32_t System_Thread::s_externalTLSCount;

static pthread_key_t s_externalTLSKey;

static pthread_once_t s_externalTLSOnce = PTHREAD_ONCE_INIT;

static const int s_niceLevels[7] = { 19, 10, 5, 0, -5, -10, -15 };

static uint32_t CurrentThreadId() {
    return OsPosixThreadId();
}

System_Thread::ThreadHandle* System_Thread::ThreadHandleCreate() {
    ThreadHandle* handle = new ThreadHandle;
    pthread_mutex_init(&handle->m_mutex, nullptr);
    pthread_cond_init(&handle->m_cond, nullptr);
    handle->m_exited = false;

    return handle;
}

void System_Thread::ThreadHandleDestroy(ThreadHandle* handle) {
    pthread_cond_destroy(&handle->m_cond);
    pthread_mutex_destroy(&handle->m_mutex);
    delete handle;
}

void System_Thread::ThreadHandleSignalExit(ThreadHandle* handle) {
    pthread_mutex_lock(&handle->m_mutex);
    handle->m_exited = true;
    pthread_cond_broadcast(&handle->m_cond);
    pthread_mutex_unlock(&handle->m_mutex);
}

void System_Thread::ThreadHandleWait(ThreadHandle* handle) {
    pthread_mutex_lock(&handle->m_mutex);

    while (!handle->m_exited) {
        pthread_cond_wait(&handle->m_cond, &handle->m_mutex);
    }

    pthread_mutex_unlock(&handle->m_mutex);
}

bool Blizzard::Thread::Sleep(uint32_t milliseconds) {
    timespec remaining;
    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
    return true;
}

bool Blizzard::Thread::IsRunning(ThreadRecord* thread) {
    return thread->m_state >= 0;
}

bool System_Thread::SetPriorityLevel(Blizzard::Thread::ThreadRecord* thread, int32_t level) {
    if (level < 0 || level > 6) {
        return false;
    }

    return setpriority(PRIO_PROCESS, thread->m_threadId, s_niceLevels[level]) == 0;
}

bool System_Thread::GetPriorityLevel(Blizzard::Thread::ThreadRecord* thread, int32_t* level) {
    *level = 3;

    return false;
}

void System_Thread::ShutdownThreadLocalStorage() {
    if (!s_initialized) {
        return;
    }

    Blizzard::Lock::ScopedMutex lock(s_mutex);

    while (s_externalTLSCount > 0) {
        ExternalTLSRecord& record = s_externalTLS[--s_externalTLSCount];

        if (record.m_destructor) {
            record.m_destructor(record.m_value);
        }
    }
}

void System_Thread::RunExternalTLSDestructors(HANDLE thread) {
    Blizzard::Lock::ScopedMutex lock(s_mutex);

    for (int32_t i = s_externalTLSCount; i != 0;) {
        i--;
        ExternalTLSRecord& record = s_externalTLS[i];

        if (record.m_thread == thread) {
            if (record.m_destructor) {
                record.m_destructor(record.m_value);
            }

            s_externalTLSCount--;
            record = s_externalTLS[s_externalTLSCount];
        }
    }
}

void System_Thread::AddToRegistry(Blizzard::Thread::ThreadRecord* thread) {
    Blizzard::Lock::ScopedMutex lock(s_registryMutex);

    if (!s_ThreadRegistry) {
        s_ThreadRegistry = new std::set<Blizzard::Thread::ThreadRecord*, std::less<Blizzard::Thread::ThreadRecord*>, Blizzard::Memory::AllocSTL_raw<Blizzard::Thread::ThreadRecord*>>();
    }

    s_ThreadRegistry->insert(thread);
}

Blizzard::Thread::ThreadRecord* System_Thread::NewThread(uint32_t (*function)(void*), void* param, const char* name) {
    if (!name) {
        name = "";
    }

    uint32_t length = static_cast<uint32_t>(strlen(name));
    Blizzard::Thread::ThreadRecord* thread = static_cast<Blizzard::Thread::ThreadRecord*>(malloc(sizeof(Blizzard::Thread::ThreadRecord) + length + 1));
    memset(thread, 0, sizeof(Blizzard::Thread::ThreadRecord));
    thread->m_param = param;
    thread->m_function = function;
    thread->m_state = 0;
    thread->m_refCount.m_value = 2;
    Blizzard::String::Copy(thread->m_name, name, length + 1);
    AddToRegistry(thread);

    return thread;
}

void System_Thread::RemoveFromRegistry(Blizzard::Thread::ThreadRecord* thread) {
    Blizzard::Lock::ScopedMutex lock(s_registryMutex);
    s_ThreadRegistry->erase(thread);

    if (s_ThreadRegistry->empty()) {
        delete s_ThreadRegistry;
        s_ThreadRegistry = nullptr;
    }
}

void System_Thread::InitMainThread() {
    s_mainThread = NewThread(nullptr, nullptr, nullptr);
    s_mainThread->m_threadId = CurrentThreadId();
    InternalSetLocalStorage(&s_threadRecordTLS, s_mainThread);
    s_mainThread->m_state = 1;
    s_mainThread->m_refCount.m_value = 1;
}

void System_Thread::InitThreadSystem() {
    if (s_initialized) {
        return;
    }

    s_initialized = true;
    Blizzard::Lock::MutexCreate(s_mutex);
    Blizzard::Lock::MutexCreate(s_registryMutex);
    AllocateLocalStorage(&s_threadRecordTLS, nullptr);
    AllocateLocalStorage(&s_stackTraceEntryPointTLS, nullptr);
    InitMainThread();
}

void System_Thread::SetStackTraceEntryPoint(void* entryPoint) {
    InitThreadSystem();
    InternalSetLocalStorage(&s_stackTraceEntryPointTLS, entryPoint);
}

Blizzard::Thread::ThreadRecord* System_Thread::GetThreadRecord() {
    InitThreadSystem();

    return static_cast<Blizzard::Thread::ThreadRecord*>(pthread_getspecific(~s_threadRecordTLS.m_key));
}

bool System_Thread::AllocateTLSSlot(Blizzard::Thread::TLSSlot* slot, Blizzard::Thread::TLSDestructor destructor) {
    InitThreadSystem();
    Blizzard::Lock::ScopedMutex lock(s_mutex);

    return slot->m_key || AllocateLocalStorage(slot, destructor);
}

void System_Thread::InitStackTraceEntryPoint() {
    void* frames[3];

    if (backtrace(frames, 3) == 3) {
        SetStackTraceEntryPoint(frames[2]);
    }
}

uint32_t __stdcall System_Thread::ThreadStarter(void* param) {
    Blizzard::Thread::ThreadRecord* thread = static_cast<Blizzard::Thread::ThreadRecord*>(param);

    thread->m_threadId = CurrentThreadId();
    InternalSetLocalStorage(&s_threadRecordTLS, thread);
    InitStackTraceEntryPoint();

    while (!thread->m_state) {
        usleep(1000);
    }

    uint32_t result = thread->m_function(thread->m_param);
    thread->m_result = result;
    RunTLSDestructors();
    thread->m_state = -1;
    ThreadHandleSignalExit(static_cast<ThreadHandle*>(thread->m_handle));
    Blizzard::Thread::Close(thread);

    return result;
}

static void* ThreadEntry(void* param) {
    System_Thread::ThreadStarter(param);

    return nullptr;
}

bool Blizzard::Thread::Create(uint32_t (*function)(void*), void* param, ThreadRecord** thread, const char* name, int32_t flags, int32_t stackSize) {
    System_Thread::InitThreadSystem();
    ThreadRecord* record = System_Thread::NewThread(function, param, name);
    *thread = record;

    if (!stackSize) {
        stackSize = System_Thread::s_DefaultStackSize;
    }

    System_Thread::ThreadHandle* handle = System_Thread::ThreadHandleCreate();
    record->m_handle = handle;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (stackSize > 0) {
        pthread_attr_setstacksize(&attr, stackSize < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : stackSize);
    }

    int error = pthread_create(&handle->m_thread, &attr, ThreadEntry, record);
    pthread_attr_destroy(&attr);

    if (error) {
        record->m_state = -1;
        System_Thread::ThreadHandleSignalExit(handle);
        Close(record);
        return true;
    }

    if (name && *name) {
        char shortName[16];
        Blizzard::String::Copy(shortName, name, sizeof(shortName));
#if !defined(__APPLE__)
        pthread_setname_np(handle->m_thread, shortName);
#else
        (void)shortName;
#endif
    }

    (*thread)->m_state = (flags & 1) == 0;

    return true;
}

static void ExternalThreadExit(void* thread) {
    System_Thread::RunExternalTLSDestructors(thread);
}

static void CreateExternalTLSKey() {
    pthread_key_create(&s_externalTLSKey, ExternalThreadExit);
}

void System_Thread::RegisterExternalTLSDestructor(void* value, Blizzard::Thread::TLSDestructor destructor) {
    pthread_once(&s_externalTLSOnce, CreateExternalTLSKey);

    Blizzard::Lock::ScopedMutex lock(s_mutex);

    if (s_externalTLSCount >= 32) {
        return;
    }

    HANDLE thread = reinterpret_cast<HANDLE>(pthread_self());
    ExternalTLSRecord& record = s_externalTLS[s_externalTLSCount++];
    record.m_thread = thread;
    record.m_event = nullptr;
    record.m_watcher = nullptr;
    record.m_threadId = CurrentThreadId();
    record.m_value = value;
    record.m_destructor = destructor;
    pthread_setspecific(s_externalTLSKey, thread);
}

void System_Thread::InternalSetLocalStorage(const Blizzard::Thread::TLSSlot* slot, const void* value) {
    pthread_setspecific(~slot->m_key, value);

    if (slot->m_destructor && value) {
        Blizzard::Thread::ThreadRecord* thread = GetThreadRecord();

        if (thread && thread->m_worker) {
            thread = thread->m_worker->m_task;
        }

        if (!thread) {
            RegisterExternalTLSDestructor(const_cast<void*>(value), slot->m_destructor);
        }
    }
}

#endif
