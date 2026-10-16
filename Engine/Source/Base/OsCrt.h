#ifndef ENGINE_SOURCE_BASE_OSCRT_H
#define ENGINE_SOURCE_BASE_OSCRT_H

#include <cstdio>
#include <type_traits>
#include <typeinfo>

#if defined(_MSC_VER)
#define RAW_TYPE_NAME(T) typeid(T).raw_name()
#else
#include <alloca.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <strings.h>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>

#define RAW_TYPE_NAME(T) typeid(T).name()

#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _strtoi64 strtoll
#define _atoi64 atoll
#define _strtoui64 strtoull
#define _finite(x) std::isfinite(x)
#define _isnan(x) std::isnan(x)
#if defined(__APPLE__)
#define _msize malloc_size
#else
#define _msize malloc_usable_size
#endif
#define _ismbcspace(c) isspace(static_cast<unsigned char>(c))

#ifndef _rotl
template <typename T>
inline T _rotl(T value, int shift) {
    constexpr int bits = static_cast<int>(sizeof(T) * 8);
    shift &= bits - 1;
    return static_cast<T>((static_cast<typename std::make_unsigned<T>::type>(value) << shift) |
        (static_cast<typename std::make_unsigned<T>::type>(value) >> (bits - shift)));
}
#endif

inline int fopen_s(FILE** stream, const char* path, const char* mode) {
    *stream = fopen(path, mode);
    return *stream ? 0 : errno;
}

inline char* _strupr(char* string) {
    for (char* c = string; *c; ++c) {
        *c = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
    }

    return string;
}

inline char* _strlwr(char* string) {
    for (char* c = string; *c; ++c) {
        *c = static_cast<char>(tolower(static_cast<unsigned char>(*c)));
    }

    return string;
}
#endif

#endif
