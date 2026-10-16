#ifndef ENGINE_SOURCE_STORM_SOURCE_SLOG_H
#define ENGINE_SOURCE_STORM_SOURCE_SLOG_H

#include <cstdint>
#include "Engine/Source/Base/OsTypes.h"
#include "Engine/Source/Storm/H/storm.h"

struct HLOCKEDLOG__ {
    /* 0000 */ int32_t unused;
};

typedef HLOCKEDLOG__* HLOCKEDLOG;

struct _LOG {
    /* 0000 */ HSLOG log;
    /* 0004 */ _LOG* next;
    /* 0008 */ char filename[MAX_PATH];
    /* 010C */ void* file;
    /* 0110 */ uint32_t flags;
    /* 0114 */ uint32_t bufferused;
    /* 0118 */ uint32_t pendpoint;
    /* 011C */ int32_t indent;
    /* 0120 */ int32_t timeStamp;
    /* 0124 */ char buffer[65536];
};

#endif
