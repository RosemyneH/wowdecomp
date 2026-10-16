#ifndef WOW_SOURCE_UI_VIDEOOPTIONS_H
#define WOW_SOURCE_UI_VIDEOOPTIONS_H

#include <cstddef>
#include <cstdint>

class CGVideoOptions {
    public:
    static void RegisterScriptFunctions();
    static void UnregisterScriptFunctions();
    static void InstallExtendedViewUiPatch();
};

#endif
