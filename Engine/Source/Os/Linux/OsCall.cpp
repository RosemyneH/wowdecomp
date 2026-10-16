#include <cstdint>
#include <new>
#include <pthread.h>
#include <unistd.h>
#include "Engine/Source/Os/OsCall.h"
#include "Engine/Source/Os/Tls.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Storm/Source/SThread.h"

static TSList<OsCallThread, TSGetLink<OsCallThread> > s_threadList;

static TSList<OsCallContext, TSGetLink<OsCallContext> > s_contextList;

static uint32_t s_tlsIndex;

static int32_t s_threadCount;

static CInitCritSect s_critsect;

// OFFSET: 0x0086F5A0
void OsCallSetContext(void* context) {
    if (!s_threadCount || !context) {
        return;
    }

    OsCallThread* thread = static_cast<OsCallThread*>(OsTlsGetValue(s_tlsIndex));

    if (!thread) {
        return;
    }

    s_critsect.Enter();
    static_cast<OsCallContext*>(context)->m_thread = thread;
    thread->m_context = static_cast<OsCallContext*>(context);
    s_critsect.Leave();
}

// OFFSET: 0x0086F5F0
void OsCallResetContext(void* context) {
    if (!s_threadCount || !context) {
        return;
    }

    OsCallThread* thread = static_cast<OsCallThread*>(OsTlsGetValue(s_tlsIndex));

    if (!thread) {
        return;
    }

    if (thread->m_context || static_cast<OsCallContext*>(context)->m_thread) {
        s_critsect.Enter();
        thread->m_context = nullptr;
        static_cast<OsCallContext*>(context)->m_thread = nullptr;
        s_critsect.Leave();
    }
}

// OFFSET: 0x0086F650
OsCallContext::~OsCallContext() {
}

// OFFSET: 0x0086F6E0
void OsCallDestroy() {
    s_critsect.Enter();

    if (s_threadCount) {
        OsCallThread* thread = static_cast<OsCallThread*>(OsTlsGetValue(s_tlsIndex));

        if (thread) {
            OsTlsSetValue(s_tlsIndex, nullptr);

            if (thread->m_context) {
                thread->m_context->m_thread = nullptr;
            }

            delete thread;
        }
    }

    if (--s_threadCount == 0) {
        OsTlsFree(s_tlsIndex);
    }

    s_critsect.Leave();
}

// OFFSET: 0x0086F760
void OsCallInitialize(const char* name) {
    s_critsect.Enter();

    if (s_threadCount++ == 0) {
        s_tlsIndex = OsTlsAlloc();
    }

    void* memory = SMemAlloc(sizeof(OsCallThread), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);

    if (!memory) {
        if (--s_threadCount == 0) {
            OsTlsFree(s_tlsIndex);
        }

        s_critsect.Leave();
        return;
    }

    OsCallThread* oldThread = static_cast<OsCallThread*>(OsTlsGetValue(s_tlsIndex));

    if (oldThread) {
        delete oldThread;
    }

    OsCallThread* thread = new (memory) OsCallThread;
    thread->m_threadId = static_cast<uint32_t>(gettid());
    thread->m_threadHandle = reinterpret_cast<void*>(pthread_self());
    SStrPrintf(thread->m_name, sizeof(thread->m_name), "%s [%u]", name, thread->m_threadId);

    OsTlsSetValue(s_tlsIndex, thread);
    s_threadList.LinkToTail(thread);

    s_critsect.Leave();
}

// OFFSET: 0x0086F880
void* OsCallInitializeContext(const char* name) {
    void* memory = SMemAlloc(sizeof(OsCallContext), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);

    if (!memory) {
        return nullptr;
    }

    OsCallContext* context = new (memory) OsCallContext;
    SStrCopy(context->m_name, name, sizeof(context->m_name));
    context->unused_000C = -1;

    s_critsect.Enter();
    s_contextList.LinkToTail(context);
    s_critsect.Leave();

    return context;
}

// OFFSET: 0x0086F910
void OsCallDestroyContext(void* context) {
    if (!context) {
        return;
    }

    s_critsect.Enter();

    OsCallThread* thread = static_cast<OsCallContext*>(context)->m_thread;

    if (thread) {
        thread->m_context = nullptr;
    }

    delete static_cast<OsCallContext*>(context);

    s_critsect.Leave();
}
