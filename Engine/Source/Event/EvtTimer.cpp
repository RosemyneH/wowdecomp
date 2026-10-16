#include <cstdint>
#include <new>
#include "Engine/Source/Event/EvtContext.h"
#include "Engine/Source/Event/EvtTimer.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

// VERIFY: 7c7d1a68 auto
// OFFSET: 0x00480F70
uint32_t IEvtTimerGetNextTime(EvtContext* context, uint32_t currTime) {
    if (!context) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    context->m_critsect.Enter();

    uint32_t nextTime = static_cast<uint32_t>(-1);

    if (context->m_timerQueue.Count()) {
        int32_t delta = context->m_timerQueue.Head()->m_targetTime.m_val - currTime;
        nextTime = delta < 0 ? 0 : delta;
    }

    context->m_critsect.Leave();

    return nextTime;
}

// VERIFY: 60bbce59 manual
// OFFSET: 0x00481100
int32_t IEvtTimerDispatch(EvtContext* context) {
    if (!context) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    context->m_critsect.Enter();

    uint32_t currTime = OsGetAsyncTimeMs();
    int32_t dispatched = 0;

    while (context->m_timerQueue.Count()) {
        EvtTimer* timer = context->m_timerQueue.Head();
        EVENTHANDLERFUNC handler = timer->m_handler;

        if (!handler && !timer->m_guidHandler) {
            context->m_timerQueue.Dequeue();

            if (timer->m_id) {
                context->m_timerIdTable.Free(timer->m_id);
            }

            continue;
        }

        if (static_cast<int32_t>(timer->m_targetTime.m_val - currTime) > 0) {
            break;
        }

        EVENT_DATA_TIMER data;
        data.m_timeout = timer->m_timeout;
        data.m_time = currTime;

        if (handler) {
            void* param = timer->m_param;

            context->m_timerQueue.Dequeue();

            if (timer->m_id) {
                context->m_timerIdTable.Free(timer->m_id);
            }

            context->m_critsect.Leave();
            handler(&data, param);
        } else {
            EVENTHANDLERGUIDFUNC guidHandler = timer->m_guidHandler;
            uint64_t guidParam = timer->m_guidParam;
            void* guidParam2 = timer->m_guidParam2;

            context->m_timerQueue.Dequeue();

            if (timer->m_id) {
                context->m_timerIdTable.Free(timer->m_id);
            }

            context->m_critsect.Leave();
            guidHandler(&data, guidParam, guidParam2);
        }

        context->m_critsect.Enter();
        dispatched = 1;
    }

    context->m_critsect.Leave();

    return dispatched;
}

// VERIFY: 063f18bf manual
// OFFSET: 0x00481290
void IEvtTimerKill(EvtContext* context, uint32_t id, EVENTHANDLERFUNC handler, const char* name) {
    if (!context) {
        DebugValidate("context");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (!id) {
        return;
    }

    context->m_critsect.Enter();

    EvtTimer* timer = context->m_timerIdTable.m_allocArray[id];

    if (timer && (timer->m_handler || timer->m_guidHandler)) {
        if ((timer->m_handler && timer->m_handler != handler) || (timer->m_guidHandler && reinterpret_cast<EVENTHANDLERFUNC>(timer->m_guidHandler) != handler)) {
            DebugLog("Error, attempt to kill eventID %d with mismatching handler (%s)!", id, name ? name : "");
        }

        timer->m_handler = nullptr;
        timer->m_guidHandler = nullptr;

        while (context->m_timerQueue.Count()) {
            EvtTimer* head = context->m_timerQueue.Head();

            if (head->m_handler || head->m_guidHandler) {
                break;
            }

            context->m_timerQueue.Dequeue();

            if (head->m_id) {
                context->m_timerIdTable.Free(head->m_id);
            }
        }
    }

    context->m_critsect.Leave();
}

// VERIFY: 1d10b345 manual
// OFFSET: 0x00481390
uint32_t IEvtTimerSet(EvtContext* context, uint32_t delay, EVENTHANDLERFUNC handler, void* param, EVENTHANDLERGUIDFUNC guidHandler, uint64_t guidParam, void* guidParam2) {
    if (!context) {
        DebugValidate("context");
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!handler && !guidHandler) {
        return 0;
    }

    context->m_critsect.Enter();

    uint32_t id = context->m_timerIdTable.Alloc();
    EvtTimer* timer = context->m_timerIdTable.m_allocArray[id];

    if (!timer) {
        void* mem = SMemAlloc(sizeof(EvtTimer), __FILE__, __LINE__, 0);

        if (!mem) {
            context->m_timerIdTable.Free(id);
            context->m_critsect.Leave();
            return 0;
        }

        timer = new (mem) EvtTimer;
        context->m_timerIdTable.m_allocArray[id] = timer;
    }

    timer->m_targetTime.SetVal(OsGetAsyncTimeMs() + delay);
    timer->m_id = id;
    timer->m_timeout = static_cast<float>(delay) * 0.001f;
    timer->m_handler = handler;
    timer->m_param = param;
    timer->m_guidHandler = guidHandler;
    timer->m_guidParam = guidParam;
    timer->m_guidParam2 = guidParam2;

    context->m_timerQueue.Enqueue(timer);

    context->m_critsect.Leave();

    return id;
}
