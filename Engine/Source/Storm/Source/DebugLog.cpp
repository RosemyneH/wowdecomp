#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Base/OsTypes.h"
#include "Engine/Source/Storm/H/storm.h"

#ifndef NDEBUG
void DebugLog(const char* format, ...) {
    char buffer[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    OutputDebugStringA(buffer);

    size_t length = strlen(buffer);

    if (length == 0 || buffer[length - 1] != '\n') {
        OutputDebugStringA("\n");
    }
}

void DebugLog(int32_t target, const char* format, ...) {
    char buffer[1024];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    DebugLog("%s", buffer);
}

void DebugValidate(const char* expression) {
    DebugLog("Validation failed: %s", expression);
}
#endif
