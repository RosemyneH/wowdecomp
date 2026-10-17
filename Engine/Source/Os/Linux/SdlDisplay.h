#pragma once

#include <SDL.h>
#include <cstdint>

// ʕ •ᴥ•ʔ✿ SDL still tries X11 first when DISPLAY is set, which is XWayland on a Wayland desktop ✿ ʕ •ᴥ•ʔ
inline void SdlUseWayland() {
#if defined(WOWDECOMP_LINUX)
    const char* chosen = SDL_getenv("SDL_VIDEODRIVER");
    if (!chosen || !chosen[0]) {
        SDL_setenv("SDL_VIDEODRIVER", "wayland", 0);
    }
#else
    (void)0;
#endif
}

// ʕ •ᴥ•ʔ✿ SDL display 0 is connector order, not the desktop origin ✿ ʕ •ᴥ•ʔ
inline int32_t SdlPrimaryDisplay() {
    int32_t count = SDL_GetNumVideoDisplays();
    if (count <= 0) {
        return 0;
    }

    int32_t origin = -1;
    int32_t largest = 0;
    int32_t largestArea = -1;

    for (int32_t i = 0; i < count; i++) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(i, &bounds) != 0 || bounds.w <= 0 || bounds.h <= 0) {
            continue;
        }

        if (bounds.x == 0 && bounds.y == 0) {
            origin = i;
        }

        int32_t area = bounds.w * bounds.h;
        if (area > largestArea) {
            largestArea = area;
            largest = i;
        }
    }

    return origin >= 0 ? origin : largest;
}

inline uint32_t SdlModeBpp(Uint32 format) {
    uint32_t bpp = SDL_BITSPERPIXEL(format);
    if (bpp == 24) {
        return 32;
    }
    return bpp;
}
