#include <cstdint>
#include "ConsoleDevice.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxFormat.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/CRect.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/Screen.h"

// VERIFY: 6674b551 auto
// OFFSET: 0x00769FF0
int32_t CCGxRestart(const char* command, const char* arguments) {
    OnGxRestartClearWorld();
    ValidateFormatMonitor(s_requestedFormat);

    if (g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
        s_lastGoodFormat = s_requestedFormat;
        UpdateGxCVars();
    } else {
        ConsoleWrite("unable to set requested display mode", DEFAULT_COLOR);
        s_requestedFormat = s_lastGoodFormat;

        if (!g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
            ConsoleWrite("unable to set last good mode", DEFAULT_COLOR);
            s_requestedFormat = s_desktopFormat;

            if (!g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
                ConsoleWrite("unable to set default format", DEFAULT_COLOR);
                s_requestedFormat = s_fallbackFormat;

                if (!g_theGxDevicePtr->DeviceSetFormat(s_requestedFormat)) {
                    goto done;
                }
            }

            s_lastGoodFormat = s_requestedFormat;
        }

        SetGxCVars(s_requestedFormat);
    }

done:
    OsGuiSetGxWindow(g_theGxDevicePtr->DeviceWindow());
    OsGuiSetWindowTitle(g_theGxDevicePtr->DeviceWindow(), s_windowTitle);
    TextureNotifyGxRestart();

    // ʕ •ᴥ•ʔ✿ The drawable stays the monitor size, so the UI has to hear the new aspect itself ✿ ʕ •ᴥ•ʔ
    CoordinateSetAspectRatio(Screen::GetAspectRatio());

    CRect rect;
    g_theGxDevicePtr->CapsWindowSize(rect);
    int32_t width = static_cast<int32_t>(rect.maxX - rect.minX);
    int32_t height = static_cast<int32_t>(rect.maxY - rect.minY);

    if (width > 0 && height > 0) {
        OsQueuePut(OS_INPUT_SIZE, width, height, 0, 0);
    }

    return 1;
}
