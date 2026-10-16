#ifndef ENGINE_SOURCE_BASE_OSTYPES_H
#define ENGINE_SOURCE_BASE_OSTYPES_H

#include "Engine/Source/Base/OsCrt.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdint>
#include <cstdio>
#include <pthread.h>
#include <signal.h>
#if defined(__linux__)
#include <ucontext.h>
#elif defined(__APPLE__)
#include <sys/ucontext.h>
#endif

#define __stdcall
#define __cdecl
#define WINAPI
#define CALLBACK

#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0x00000000
#define WAIT_TIMEOUT 0x00000102
#define WAIT_FAILED 0xFFFFFFFF
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)))

typedef void* HANDLE;
typedef int32_t BOOL;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef pthread_mutex_t CRITICAL_SECTION;
typedef ucontext_t CONTEXT;

struct HINSTANCE__;
typedef HINSTANCE__* HINSTANCE;

struct HWND__;
typedef HWND__* HWND;

union LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    int64_t QuadPart;
};

struct SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
};

struct OVERLAPPED {
    uintptr_t Internal;
    uintptr_t InternalHigh;
    DWORD Offset;
    DWORD OffsetHigh;
    HANDLE hEvent;
};

struct EXCEPTION_POINTERS {
    siginfo_t* ExceptionRecord;
    CONTEXT* ContextRecord;
};

inline void OutputDebugStringA(const char* string) {
    fputs(string, stderr);
}

template <typename T, typename V>
inline T InterlockedExchange(volatile T* target, V value) {
    return __atomic_exchange_n(target, static_cast<T>(value), __ATOMIC_SEQ_CST);
}

template <typename T, typename V>
inline T InterlockedExchangeAdd(volatile T* target, V value) {
    return __atomic_fetch_add(target, static_cast<T>(value), __ATOMIC_SEQ_CST);
}

template <typename T>
inline T InterlockedIncrement(volatile T* target) {
    return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T InterlockedDecrement(volatile T* target) {
    return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST);
}

template <typename T, typename V, typename C>
inline T InterlockedCompareExchange(volatile T* target, V exchange, C comparand) {
    T expected = static_cast<T>(comparand);
    __atomic_compare_exchange_n(target, &expected, static_cast<T>(exchange), false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return expected;
}

inline void* InterlockedCompareExchangePointer(void* volatile* target, void* exchange, void* comparand) {
    __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
#endif

#endif
