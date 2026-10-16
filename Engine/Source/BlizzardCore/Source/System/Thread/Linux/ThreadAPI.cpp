#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdlib>
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Linux/ThreadHandle.h"

bool Blizzard::Thread::Close(ThreadRecord* thread) {
    if (thread->m_refCount.Decrement() > 0) {
        return true;
    }

    if (thread->m_handle) {
        System_Thread::ThreadHandleDestroy(static_cast<System_Thread::ThreadHandle*>(thread->m_handle));
        thread->m_handle = nullptr;
    }

    System_Thread::RemoveFromRegistry(thread);
    free(thread);

    return true;
}

bool Blizzard::Thread::WaitForever(ThreadRecord* thread) {
    if (thread->m_state < 0) {
        return true;
    }

    System_Thread::ThreadWorker* worker = thread->m_worker;

    if (worker && thread != worker->m_thread) {
        if (thread == worker->m_task) {
            Blizzard::Lock::EventWaitForever(worker->m_readyEvent.m_event);
            Blizzard::Lock::EventSet(worker->m_readyEvent.m_event);
        }

        return true;
    }

    if (!thread->m_handle) {
        return false;
    }

    System_Thread::ThreadHandleWait(static_cast<System_Thread::ThreadHandle*>(thread->m_handle));

    return true;
}

#endif
