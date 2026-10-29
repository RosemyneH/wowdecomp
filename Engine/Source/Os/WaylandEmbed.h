#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct WaylandDmabuf {
    uint64_t key = 0;
    int32_t width = 0;
    int32_t height = 0;
    uint32_t format = 0;
    uint64_t modifier = 0;
    int32_t planes = 0;
    int32_t fd[4] = { -1, -1, -1, -1 };
    uint32_t offset[4] = {};
    uint32_t stride[4] = {};
};

struct WaylandPixels {
    const uint8_t* data = nullptr;
    const WaylandDmabuf* dmabuf = nullptr;
    int32_t cropX = 0;
    int32_t cropY = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;
    uint32_t serial = 0;
};

struct WaylandApp {
    std::string id;
    std::string name;
    std::string comment;
    std::string exec;
};

#if defined(WOWDECOMP_LINUX)

int32_t WaylandEmbedAvailable();
int32_t WaylandEmbedOpen(const char* spec, int32_t width, int32_t height, char* error, uint32_t errorLen);
void WaylandEmbedClose(int32_t id);
void WaylandEmbedService();
int32_t WaylandEmbedPixels(int32_t id, WaylandPixels* out);
void WaylandEmbedPointer(int32_t id, int32_t x, int32_t y, int32_t button, int32_t down);
void WaylandEmbedScroll(int32_t id, int32_t delta);
void WaylandEmbedResize(int32_t id, int32_t width, int32_t height);
void WaylandEmbedFocus(int32_t id, int32_t focused);
void WaylandEmbedClearFocus();
void WaylandEmbedKeySym(int32_t id, uint32_t keysym, int32_t down);
int32_t WaylandEmbedTakeKey(int32_t sdlSym, int32_t evdev, int32_t down);
int32_t WaylandEmbedRelativeActive();
int32_t WaylandEmbedHasFocus();
void WaylandEmbedRelativeMotion(double dx, double dy);
void WaylandEmbedPointerButton(int32_t button, int32_t down);
void WaylandEmbedSetDmabufFormats(uint64_t device, const uint32_t* formats, const uint64_t* modifiers, uint32_t count);
int32_t WaylandEmbedTakeDeadDmabuf(uint64_t* key);
const char* WaylandEmbedTitle(int32_t id);
const std::vector<WaylandApp>& WaylandEmbedApps(int32_t rescan);

#else

inline int32_t WaylandEmbedAvailable() { return 0; }
inline int32_t WaylandEmbedOpen(const char*, int32_t, int32_t, char* error, uint32_t errorLen) {
    if (error && errorLen) std::strncpy(error, "wayland frames are a Linux host", errorLen - 1);
    return 0;
}
inline void WaylandEmbedClose(int32_t) {}
inline void WaylandEmbedService() {}
inline int32_t WaylandEmbedPixels(int32_t, WaylandPixels*) { return 0; }
inline void WaylandEmbedPointer(int32_t, int32_t, int32_t, int32_t, int32_t) {}
inline void WaylandEmbedScroll(int32_t, int32_t) {}
inline void WaylandEmbedResize(int32_t, int32_t, int32_t) {}
inline void WaylandEmbedFocus(int32_t, int32_t) {}
inline void WaylandEmbedClearFocus() {}
inline void WaylandEmbedKeySym(int32_t, uint32_t, int32_t) {}
inline int32_t WaylandEmbedTakeKey(int32_t, int32_t, int32_t) { return 0; }
inline int32_t WaylandEmbedRelativeActive() { return 0; }
inline int32_t WaylandEmbedHasFocus() { return 0; }
inline void WaylandEmbedRelativeMotion(double, double) {}
inline void WaylandEmbedPointerButton(int32_t, int32_t) {}
inline void WaylandEmbedSetDmabufFormats(uint64_t, const uint32_t*, const uint64_t*, uint32_t) {}
inline int32_t WaylandEmbedTakeDeadDmabuf(uint64_t*) { return 0; }
inline const char* WaylandEmbedTitle(int32_t) { return ""; }
inline const std::vector<WaylandApp>& WaylandEmbedApps(int32_t) {
    static const std::vector<WaylandApp> none;
    return none;
}

#endif
