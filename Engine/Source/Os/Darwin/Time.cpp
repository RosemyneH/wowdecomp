#include <cstdint>
#include <ctime>
#include "Engine/Source/Os/Time.h"

int32_t OsGetTimeZone() {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);

    return static_cast<int32_t>(local.tm_gmtoff / 60);
}

// OFFSET: 0x0086D490
void OsGetTimeStamp(char* buffer, uint32_t size) {
    time_t now = time(nullptr);
    struct tm local;
    strftime(buffer, size, "%m%d%y_%H%M%S", localtime_r(&now, &local));
}
