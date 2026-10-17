#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

inline bool SdlEnvSet(const char* name) {
    const char* value = SDL_getenv(name);
    return value && value[0];
}

inline bool SdlEnvIs(const char* name, const char* expected) {
    const char* value = SDL_getenv(name);
    if (!value) return false;
    while (*value && *expected) {
        char left = *value++;
        char right = *expected++;
        if (left >= 'A' && left <= 'Z') left = static_cast<char>(left - 'A' + 'a');
        if (right >= 'A' && right <= 'Z') right = static_cast<char>(right - 'A' + 'a');
        if (left != right) return false;
    }
    return *value == '\0' && *expected == '\0';
}

// ʕ •ᴥ•ʔ✿ DISPLAY is set on both X11 and XWayland, so the session type picks the window ✿ ʕ •ᴥ•ʔ
inline void SdlUseWayland() {
#if defined(__ANDROID__)
    return;
#else
    if (SdlEnvSet("SDL_VIDEODRIVER")) return;
    if (SdlEnvIs("XDG_SESSION_TYPE", "wayland") && SdlEnvSet("WAYLAND_DISPLAY")) {
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "wayland", 0);
        return;
    }
    if (SdlEnvIs("XDG_SESSION_TYPE", "x11") || (SdlEnvSet("DISPLAY") && !SdlEnvSet("WAYLAND_DISPLAY"))) {
        SDL_setenv_unsafe("SDL_VIDEODRIVER", "x11", 0);
        return;
    }
    if (SdlEnvSet("WAYLAND_DISPLAY")) SDL_setenv_unsafe("SDL_VIDEODRIVER", "wayland", 0);
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
