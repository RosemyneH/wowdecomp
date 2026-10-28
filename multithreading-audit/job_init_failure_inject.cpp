#include <atomic>
#include <cerrno>
#include <dlfcn.h>
#include <pthread.h>

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                              void* (*start)(void*), void* arg) {
    static std::atomic<unsigned> attempts{0};
    if (++attempts == 2) return EAGAIN;
    using Create = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
    auto create = reinterpret_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    return create(thread, attr, start, arg);
}
