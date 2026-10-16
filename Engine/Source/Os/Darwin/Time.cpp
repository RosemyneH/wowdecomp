#include <cstdint>
#include <ctime>
#include "Engine/Source/Os/Time.h"

int32_t OsGetTimeZone() {
    time_t now = time(nullptr);
    struct tm local;
    if (!localtime_r(&now, &local)) {
        return 0;
    }

    return static_cast<int32_t>(local.tm_gmtoff / 60);
}

// OFFSET: 0x0086D490
void OsGetTimeStamp(char* buffer, uint32_t size) {
    time_t now = time(nullptr);
    struct tm local;
    if (!localtime_r(&now, &local)) {
        if (size) {
            buffer[0] = '\0';
        }
        return;
    }
    strftime(buffer, size, "%m%d%y_%H%M%S", &local);
}
