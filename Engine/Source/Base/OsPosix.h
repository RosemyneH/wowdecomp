#pragma once
#include <cstdint>
#include <pthread.h>

#if defined(__APPLE__)
#include <mach/mach.h>

inline uint32_t OsPosixThreadId() {
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<uint32_t>(tid);
}
inline void OsPosixSetThreadName(const char* name) { pthread_setname_np(name); }
#else
#include <sys/syscall.h>
#include <unistd.h>

inline uint32_t OsPosixThreadId() { return static_cast<uint32_t>(syscall(SYS_gettid)); }
inline void OsPosixSetThreadName(const char* name) { pthread_setname_np(pthread_self(), name); }
#endif
