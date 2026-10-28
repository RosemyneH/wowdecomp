#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <pthread.h>

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                              void* (*start)(void*), void* arg) {
    static std::atomic<unsigned> attempts{0};
    const char* failureAttempt = std::getenv("JOB_FAIL_CREATE_ATTEMPT");
    if (++attempts == (failureAttempt ? std::strtoul(failureAttempt, nullptr, 10) : 2)) return EAGAIN;
    using Create = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    auto create = reinterpret_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    return create(thread, attr, start, arg);
}
