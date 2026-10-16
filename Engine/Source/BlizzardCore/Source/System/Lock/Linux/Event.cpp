#if defined(__linux__) || defined(__APPLE__)

#include <cerrno>
#include <cstdint>
#include <ctime>
#include <pthread.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

struct EventState {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
};

static EventState* NewEventState() {
    EventState* state = new EventState;
    pthread_mutex_init(&state->mutex, nullptr);

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#if !defined(__APPLE__)
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    pthread_cond_init(&state->cond, &attr);
    pthread_condattr_destroy(&attr);

    state->signaled = false;

    return state;
}

void Blizzard::Lock::EventCreate(Event& event) {
    event = NewEventState();
}

void Blizzard::Lock::EventDestroy(Event& event) {
    EventState* state = static_cast<EventState*>(event);

    if (!state) {
        return;
    }

    pthread_cond_destroy(&state->cond);
    pthread_mutex_destroy(&state->mutex);
    delete state;
    event = nullptr;
}

void Blizzard::Lock::EventSet(Event& event) {
    EventState* state = static_cast<EventState*>(event);
    pthread_mutex_lock(&state->mutex);
    state->signaled = true;
    pthread_cond_signal(&state->cond);
    pthread_mutex_unlock(&state->mutex);
}

void Blizzard::Lock::EventWaitForever(Event& event) {
    EventState* state = static_cast<EventState*>(event);
    pthread_mutex_lock(&state->mutex);

    while (!state->signaled) {
        pthread_cond_wait(&state->cond, &state->mutex);
    }

    state->signaled = false;
    pthread_mutex_unlock(&state->mutex);
}

Blizzard::Lock::Event* Blizzard::Lock::EventWait(uint32_t timeout, Event& event) {
    if (timeout == INFINITE) {
        EventWaitForever(event);
        return &event;
    }

    EventState* state = static_cast<EventState*>(event);

    timespec deadline;
#if defined(__APPLE__)
    clock_gettime(CLOCK_REALTIME, &deadline);
#else
    clock_gettime(CLOCK_MONOTONIC, &deadline);
#endif
    deadline.tv_sec += timeout / 1000;
    deadline.tv_nsec += static_cast<long>(timeout % 1000) * 1000000;

    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000;
    }

    pthread_mutex_lock(&state->mutex);

    while (!state->signaled) {
        if (pthread_cond_timedwait(&state->cond, &state->mutex, &deadline) == ETIMEDOUT) {
            break;
        }
    }

    bool signaled = state->signaled;
    state->signaled = false;
    pthread_mutex_unlock(&state->mutex);

    return signaled ? &event : nullptr;
}

Blizzard::Lock::EventObject::EventObject() {
    EventCreate(this->m_event);
}

Blizzard::Lock::EventObject::~EventObject() {
    EventDestroy(this->m_event);
}

#endif
