#ifndef ENGINE_SOURCE_STORM_SOURCE_LINUX_SSYNC_H
#define ENGINE_SOURCE_STORM_SOURCE_LINUX_SSYNC_H

#include <cstdint>
#include "Engine/Source/Base/OsTypes.h"

enum SSYNCTYPE {
    SSYNC_EVENT = 0,
    SSYNC_MUTEX = 1,
    SSYNC_SEMAPHORE = 2,
    SSYNC_THREAD = 3,
};

HANDLE SSyncCreate(SSYNCTYPE type, int32_t count, int32_t maximum, int32_t manualReset);
void SSyncAddRef(HANDLE handle);
void SSyncClose(HANDLE handle);
int32_t SSyncSignal(HANDLE handle, int32_t count);
int32_t SSyncReset(HANDLE handle);
int32_t SSyncRelease(HANDLE handle);
uint32_t SSyncWait(HANDLE handle, uint32_t timeout);
uint32_t SSyncWaitMultiple(uint32_t count, HANDLE* handles, int32_t waitAll, uint32_t timeout);

#endif
