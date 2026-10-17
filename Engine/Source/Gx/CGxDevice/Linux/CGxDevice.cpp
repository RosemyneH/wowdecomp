#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <SDL.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Os/Linux/SdlDisplay.h"

static int32_t InitVideo() {
    SdlUseWayland();
    return SDL_WasInit(SDL_INIT_VIDEO) || SDL_InitSubSystem(SDL_INIT_VIDEO) == 0;
}

static uint32_t ReadSysfsHex(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) {
        return 0;
    }
    unsigned int value = 0;
    if (fscanf(file, "%x", &value) != 1) {
        value = 0;
    }
    fclose(file);
    return value;
}

static int CGxMonitorModeSort(const void* a, const void* b) {
    const CGxMonitorMode* modeA = static_cast<const CGxMonitorMode*>(a);
    const CGxMonitorMode* modeB = static_cast<const CGxMonitorMode*>(b);

    return modeA->m_size.x * modeA->m_size.y - modeB->m_size.x * modeB->m_size.y;
}

int32_t CGxDevice::AdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& formats) {
    formats.SetCount(0);
    formats.m_chunk = 64;
    formats.Reserve(256, 0);

#if defined(WOWDECOMP_VULKAN)
    if (api == GxApi_Vulkan) {
        CGxDevice::VulkanAdapterFormats(formats);
    }
#endif

    return formats.Count() != 0;
}

int32_t CGxDevice::DeviceAdapterID(uint16_t& vendorID, uint16_t& deviceID, uint32_t& driverVersionHi, uint32_t& driverVersionLo) {
    driverVersionHi = 0;
    driverVersionLo = 0;
    vendorID = static_cast<uint16_t>(ReadSysfsHex("/sys/class/drm/card0/device/vendor"));
    deviceID = static_cast<uint16_t>(ReadSysfsHex("/sys/class/drm/card0/device/device"));

    int32_t result = vendorID && deviceID;
    if (!result) {
        vendorID = 0xFFFF;
        deviceID = 0xFFFF;
    }

    CGxDevice::Log("CGxDevice::DeviceAdapterID(): RET: %d, VID: %x, DID: %x, DVER: %x.%x", result, vendorID, deviceID, driverVersionHi, driverVersionLo);

    return result;
}

int32_t CGxDevice::DeviceAdapterInfer(uint16_t& deviceID) {
    deviceID = 3;
    CGxDevice::Log("CGxDevice::DeviceAdapterInfer(): RET: %d, DID: %x", 1, deviceID);
    return 1;
}

int32_t CGxDevice::AdapterDesktopMode(CGxMonitorMode& mode) {
    SDL_DisplayMode displayMode;

    if (!InitVideo() || SDL_GetDesktopDisplayMode(SdlPrimaryDisplay(), &displayMode) != 0) {
        return 0;
    }

    mode.m_size.x = displayMode.w;
    mode.m_size.y = displayMode.h;
    mode.m_refreshRate = displayMode.refresh_rate;
    mode.m_bpp = SdlModeBpp(displayMode.format);

    return 1;
}

int32_t CGxDevice::AdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& modes) {
    modes.SetCount(0);

    if (!InitVideo()) {
        return 0;
    }

    int32_t display = SdlPrimaryDisplay();
    int32_t count = SDL_GetNumDisplayModes(display);
    for (int32_t i = 0; i < count; i++) {
        SDL_DisplayMode displayMode;
        if (SDL_GetDisplayMode(display, i, &displayMode) != 0) {
            continue;
        }

        uint32_t bpp = SdlModeBpp(displayMode.format);
        if (displayMode.w >= 640 && displayMode.h >= 480 && bpp >= 16) {
            CGxMonitorMode* mode = modes.New();
            mode->m_size.x = displayMode.w;
            mode->m_size.y = displayMode.h;
            mode->m_bpp = bpp;
            mode->m_refreshRate = displayMode.refresh_rate;
        }
    }

    qsort(modes.Ptr(), modes.Count(), sizeof(CGxMonitorMode), CGxMonitorModeSort);

    return modes.Count() != 0;
}
