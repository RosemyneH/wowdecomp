#if defined(__linux__) || defined(__APPLE__)

#include <pthread.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"

void Blizzard::Lock::MutexCreate(Mutex& mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

void Blizzard::Lock::MutexEnter(Mutex& mutex) {
    pthread_mutex_lock(&mutex);
}

void Blizzard::Lock::MutexLeave(Mutex& mutex) {
    pthread_mutex_unlock(&mutex);
}

void Blizzard::Lock::MutexDestroy(Mutex& mutex) {
    pthread_mutex_destroy(&mutex);
}

Blizzard::Lock::ScopedMutex::ScopedMutex(Mutex& mutex) {
    this->m_mutex = &mutex;
    MutexEnter(mutex);
}

Blizzard::Lock::ScopedMutex::~ScopedMutex() {
    MutexLeave(*this->m_mutex);
}

Blizzard::Lock::RMutex::RMutex() {
    MutexCreate(this->m_mutex);
}

Blizzard::Lock::RMutex::~RMutex() {
    MutexDestroy(this->m_mutex);
}

#endif
