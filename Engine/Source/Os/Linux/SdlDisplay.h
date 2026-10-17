#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

// ʕ •ᴥ•ʔ✿ SDL still tries X11 first when DISPLAY is set, which is XWayland on a Wayland desktop ✿ ʕ •ᴥ•ʔ
inline void SdlUseWayland() {
#if defined(__ANDROID__)
    return;
#else
    const char* chosen = SDL_getenv("SDL_VIDEODRIVER");
    if (!chosen || !chosen[0]) {
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "wayland", 0);
    }
#endif
}

inline SDL_DisplayID SdlPrimaryDisplay() {
    return SDL_GetPrimaryDisplay();
}

inline uint32_t SdlModeBpp(SDL_PixelFormat format) {
    const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(format);
    uint32_t bpp = details ? details->bits_per_pixel : 32;
    if (bpp == 24) {
        return 32;
    }
    return bpp;
}
