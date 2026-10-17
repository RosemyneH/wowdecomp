#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdlib>
#include <pthread.h>
#include <sched.h>
#include "Engine/Source/BlizzardCore/Source/System/Hardware/Hardware.h"
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/ThreadLocalStorage.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Linux/ThreadHandle.h"

static void SetThreadAffinityMask(Blizzard::Thread::ThreadRecord* thread, uint32_t mask) {
#if defined(__APPLE__)
    (void)thread; (void)mask;
#else
    cpu_set_t set;
    CPU_ZERO(&set);

    for (uint32_t cpu = 0; cpu < 32; cpu++) {
        if (mask & (1u << cpu)) {
            CPU_SET(cpu, &set);
        }
    }

#if defined(__ANDROID__)
    sched_setaffinity(pthread_gettid_np(static_cast<System_Thread::ThreadHandle*>(thread->m_handle)->m_thread), sizeof(set), &set);
#else
    pthread_setaffinity_np(static_cast<System_Thread::ThreadHandle*>(thread->m_handle)->m_thread, sizeof(set), &set);
#endif
#endif
}

System_Thread::ThreadWorker::~ThreadWorker() {
    Blizzard::Lock::EventSet(this->m_wakeEvent.m_event);
    Blizzard::Lock::EventWaitForever(this->m_readyEvent.m_event);

    if (this->m_thread) {
        Blizzard::Lock::EventSet(this->m_wakeEvent.m_event);
        ThreadHandleWait(static_cast<ThreadHandle*>(this->m_thread->m_handle));
        Blizzard::Thread::Close(this->m_thread);
        this->m_thread = nullptr;
    }
}

uint32_t System_Thread::ThreadWorker::Executive(void* param) {
    ThreadWorker* worker = static_cast<ThreadWorker*>(param);

    Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);
    Blizzard::Lock::EventWaitForever(worker->m_wakeEvent.m_event);

    while (worker->m_taskFunction) {
        worker->m_task->m_state = 1;
        worker->m_task->m_result = worker->m_taskFunction(worker->m_taskParam);

        if (worker->m_thread) {
            SetPriorityLevel(worker->m_thread, worker->m_priority);
        }

        if (worker->m_thread) {
            int32_t affinity = worker->m_affinity;

            if (affinity > 0) {
                SetThreadAffinityMask(worker->m_thread, affinity);
            } else {
                Blizzard::Hardware::GetProcessorFeatures();

                if (affinity <= static_cast<int32_t>(System_Hardware::s_processorFeatures.m_processorCount)) {
                    SetThreadAffinityMask(worker->m_thread, affinity);
                }
            }
        }

        worker->m_task->m_state = -1;
        Blizzard::Thread::Close(worker->m_task);
        worker->m_task = nullptr;
        worker->m_taskFunction = nullptr;
        worker->m_taskParam = nullptr;

        {
            Blizzard::Lock::ScopedMutex lock(worker->m_pool->m_mutex.m_mutex);
            worker->m_pool->m_idle.push_back(worker);
        }

        Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);
        Blizzard::Lock::EventWaitForever(worker->m_wakeEvent.m_event);
    }

    Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);

    return 0;
}

bool System_Thread::ThreadWorker::Start(Blizzard::Thread::ThreadPoolRecord* pool, int32_t flags, int32_t stackSize) {
    if (!Blizzard::Thread::Create(Executive, this, &this->m_thread, "Blizzard Thread Pool Worker", flags, stackSize)) {
        return false;
    }

    if (this->m_thread) {
        GetPriorityLevel(this->m_thread, &this->m_priority);
    }

    if (this->m_thread) {
#if defined(__APPLE__)
        uint32_t mask = 0xFFFFFFFF;
#else
        cpu_set_t set;
        uint32_t mask = 0;

        if (!sched_getaffinity(0, sizeof(set), &set)) {
            for (uint32_t cpu = 0; cpu < 32; cpu++) {
                if (CPU_ISSET(cpu, &set)) {
                    mask |= 1u << cpu;
                }
            }
        }

#endif
        this->m_affinity = static_cast<int32_t>(mask);
    }

    this->m_pool = pool;

    return true;
}

#endif
