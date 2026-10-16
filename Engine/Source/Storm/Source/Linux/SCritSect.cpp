#if defined(__linux__) || defined(__APPLE__)

#include <pthread.h>
#include "Engine/Source/Storm/H/SCritSect.h"

CCritSect::CCritSect() {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&this->m_critsect, &attr);
    pthread_mutexattr_destroy(&attr);
}

CCritSect::~CCritSect() {
    pthread_mutex_destroy(&this->m_critsect);
}

void CCritSect::Enter() {
    pthread_mutex_lock(&this->m_critsect);
}

void CCritSect::Enter(int32_t forwriting) {
    pthread_mutex_lock(&this->m_critsect);
}

void CCritSect::Leave() {
    pthread_mutex_unlock(&this->m_critsect);
}

void CCritSect::Leave(int32_t fromwriting) {
    pthread_mutex_unlock(&this->m_critsect);
}

#endif
