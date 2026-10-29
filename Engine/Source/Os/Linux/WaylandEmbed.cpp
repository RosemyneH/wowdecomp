#include "Engine/Source/Os/WaylandEmbed.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>
#include <dirent.h>
#include <algorithm>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-version.h>
#include <xkbcommon/xkbcommon.h>

extern "C" {
#include "protocol/xdg-decoration-server.h"
#include "protocol/xdg-shell-server.h"
}

extern char** environ;

namespace {

constexpr int32_t kMaxEdge = 2560;
constexpr int32_t kOutputW = 2560;
constexpr int32_t kOutputH = 1600;

struct Host;
struct XdgRole;

struct Surface {
    wl_resource* resource = nullptr;
    wl_resource* buffer = nullptr;
    wl_resource* frame = nullptr;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t geoX = 0;
    int32_t geoY = 0;
    int32_t geoW = 0;
    int32_t geoH = 0;
    std::vector<uint8_t> pixels;
    bool mapped = false;
    Host* host = nullptr;
};

struct ClientState {
    Host* host = nullptr;
    wl_client* client = nullptr;
    wl_listener gone;
    wl_resource* pointer = nullptr;
    wl_resource* keyboard = nullptr;
    wl_resource* toplevelSurface = nullptr;
    Surface* entered = nullptr;
    bool keysIn = false;
};

struct Host {
    int32_t id = 0;
    wl_display* display = nullptr;
    struct {
        wl_listener listener;
        Host* self;
    } created;
    std::string socket;
    pid_t pid = 0;
    int32_t reqW = 640;
    int32_t reqH = 400;
    std::vector<uint8_t> frame;
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;
    int32_t cropX = 0;
    int32_t cropY = 0;
    uint32_t serial = 1;
    uint32_t stamp = 1;
    bool focused = false;
    char title[160] = "Wayland";
    xkb_context* xkb = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* xkbState = nullptr;
    std::string keymapText;
    wl_resource* wm = nullptr;
    std::vector<ClientState*> clients;
    std::vector<Surface*> surfaces;
    std::vector<XdgRole*> roles;
};

std::vector<Host*> g_hosts;
int32_t g_nextId = 1;
int32_t g_focus = 0;

void Note(char* error, uint32_t errorLen, const char* text) {
    if (!error || !errorLen) return;
    std::snprintf(error, errorLen, "%s", text ? text : "");
}

Host* HostForClient(wl_client* client);

Host* FindHost(int32_t id) {
    for (Host* host : g_hosts) if (host->id == id) return host;
    return nullptr;
}

uint32_t NextSerial(Host* host) { return ++host->serial; }

uint32_t NowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

Surface* SurfaceOf(wl_resource* resource) {
    return static_cast<Surface*>(wl_resource_get_user_data(resource));
}

void DestroySurface(wl_resource* resource) {
    Surface* surface = SurfaceOf(resource);
    if (surface && surface->host) {
        auto& list = surface->host->surfaces;
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (*it == surface) {
                list.erase(it);
                break;
            }
        }
    }
    delete surface;
}

void SurfaceDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }

void SurfaceAttach(wl_client*, wl_resource* resource, wl_resource* buffer, int32_t, int32_t) {
    Surface* surface = SurfaceOf(resource);
    if (surface) surface->buffer = buffer;
}

void SurfaceDamage(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void SurfaceOpaque(wl_client*, wl_resource*, wl_resource*) {}
void SurfaceInput(wl_client*, wl_resource*, wl_resource*) {}
void SurfaceTransform(wl_client*, wl_resource*, int32_t) {}
void SurfaceScale(wl_client*, wl_resource*, int32_t) {}
void SurfaceDamageBuffer(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void SurfaceOffset(wl_client*, wl_resource*, int32_t, int32_t) {}
#if WAYLAND_VERSION_MINOR >= 25
void SurfaceGetRelease(wl_client*, wl_resource*, uint32_t) {}
#endif

void SurfaceFrame(wl_client* client, wl_resource* resource, uint32_t id) {
    Surface* surface = SurfaceOf(resource);
    if (!surface) return;
    if (surface->frame) wl_resource_destroy(surface->frame);
    surface->frame = wl_resource_create(client, &wl_callback_interface, 1, id);
}

void CopyBuffer(Surface* surface) {
    if (!surface || !surface->buffer) return;
    wl_shm_buffer* shm = wl_shm_buffer_get(surface->buffer);
    if (!shm) return;
    int32_t width = wl_shm_buffer_get_width(shm);
    int32_t height = wl_shm_buffer_get_height(shm);
    int32_t stride = wl_shm_buffer_get_stride(shm);
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || stride < width * 4) return;
    wl_shm_buffer_begin_access(shm);
    auto* src = static_cast<const uint8_t*>(wl_shm_buffer_get_data(shm));
    surface->pixels.resize((size_t)width * (size_t)height * 4);
    surface->width = width;
    surface->height = height;
    for (int32_t y = 0; y < height; y++) {
        std::memcpy(surface->pixels.data() + (size_t)y * (size_t)width * 4, src + (size_t)y * (size_t)stride, (size_t)width * 4);
    }
    uint32_t format = wl_shm_buffer_get_format(shm);
    if (format == WL_SHM_FORMAT_XRGB8888) {
        for (size_t i = 3; i < surface->pixels.size(); i += 4) surface->pixels[i] = 255;
    }
    wl_shm_buffer_end_access(shm);
    surface->mapped = true;
    wl_buffer_send_release(surface->buffer);
    surface->buffer = nullptr;
}

Host* HostForClient(wl_client* client) {
    for (Host* host : g_hosts) {
        for (ClientState* state : host->clients) if (state->client == client) return host;
    }
    return nullptr;
}

void Publish(Host* host) {
    Surface* best = nullptr;
    int32_t bestArea = 0;
    for (Surface* surface : host->surfaces) {
        if (!surface->mapped) continue;
        int32_t area = surface->width * surface->height;
        if (area > bestArea) {
            best = surface;
            bestArea = area;
        }
    }
    if (!best) return;
    // ʕ •ᴥ•ʔ✿ Clients that draw their own shadow name the real window inside the buffer ✿ ʕ •ᴥ•ʔ
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = best->width;
    int32_t h = best->height;
    if (best->geoW > 0 && best->geoH > 0 && best->geoX >= 0 && best->geoY >= 0 && best->geoX + best->geoW <= best->width && best->geoY + best->geoH <= best->height) {
        x = best->geoX;
        y = best->geoY;
        w = best->geoW;
        h = best->geoH;
    }
    host->width = w;
    host->height = h;
    host->stride = w * 4;
    host->cropX = x;
    host->cropY = y;
    if (x == 0 && y == 0 && w == best->width && h == best->height) {
        host->frame = best->pixels;
    } else {
        host->frame.resize((size_t)w * (size_t)h * 4);
        for (int32_t row = 0; row < h; row++) {
            std::memcpy(host->frame.data() + (size_t)row * (size_t)w * 4, best->pixels.data() + ((size_t)(y + row) * (size_t)best->width + (size_t)x) * 4, (size_t)w * 4);
        }
    }
    host->stamp++;
}

void SurfaceCommit(wl_client*, wl_resource* resource) {
    Surface* surface = SurfaceOf(resource);
    if (!surface) return;
    CopyBuffer(surface);
    if (surface->frame) {
        wl_callback_send_done(surface->frame, NowMs());
        wl_resource_destroy(surface->frame);
        surface->frame = nullptr;
    }
    Host* host = HostForClient(wl_resource_get_client(resource));
    if (host) Publish(host);
}

const struct wl_surface_interface kSurfaceImpl = {
    SurfaceDestroy, SurfaceAttach, SurfaceDamage, SurfaceFrame, SurfaceOpaque, SurfaceInput, SurfaceCommit,
    SurfaceTransform, SurfaceScale, SurfaceDamageBuffer, SurfaceOffset,
#if WAYLAND_VERSION_MINOR >= 25
    SurfaceGetRelease,
#endif
};

void CompositorCreateSurface(wl_client* client, wl_resource*, uint32_t id) {
    auto* surface = new Surface();
    surface->resource = wl_resource_create(client, &wl_surface_interface, 4, id);
    if (!surface->resource) {
        delete surface;
        wl_client_post_no_memory(client);
        return;
    }
    surface->host = HostForClient(client);
    if (surface->host) surface->host->surfaces.push_back(surface);
    wl_resource_set_implementation(surface->resource, &kSurfaceImpl, surface, DestroySurface);
}

void RegionDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
void RegionAdd(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void RegionSubtract(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}

const struct wl_region_interface kRegionImpl = { RegionDestroy, RegionAdd, RegionSubtract };

void CompositorCreateRegion(wl_client* client, wl_resource*, uint32_t id) {
    wl_resource* region = wl_resource_create(client, &wl_region_interface, 1, id);
    if (!region) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(region, &kRegionImpl, nullptr, nullptr);
}

#if WAYLAND_VERSION_MINOR >= 25
void CompositorRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
#endif

const struct wl_compositor_interface kCompositorImpl = {
    CompositorCreateSurface,
    CompositorCreateRegion,
#if WAYLAND_VERSION_MINOR >= 25
    CompositorRelease,
#endif
};

void OutputRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
const struct wl_output_interface kOutputImpl = { OutputRelease };

void BindOutput(wl_client* client, void* data, uint32_t version, uint32_t id) {
    Host* host = static_cast<Host*>(data);
    wl_resource* output = wl_resource_create(client, &wl_output_interface, version < 2 ? version : 2, id);
    if (!output) return;
    wl_resource_set_implementation(output, &kOutputImpl, host, nullptr);
    // ʕ •ᴥ•ʔ✿ Clients cap their window at the output, so report room for the largest frame ✿ ʕ •ᴥ•ʔ
    wl_output_send_geometry(output, 0, 0, kOutputW, kOutputH, WL_OUTPUT_SUBPIXEL_UNKNOWN, "wowdecomp", "frame", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(output, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, kOutputW, kOutputH, 60000);
    if (version >= 2) {
        wl_output_send_scale(output, 1);
        wl_output_send_done(output);
    }
}

ClientState* StateFor(wl_client* client) {
    Host* host = HostForClient(client);
    if (!host) return nullptr;
    for (ClientState* state : host->clients) if (state->client == client) return state;
    return nullptr;
}

void SendKeymap(Host* host, wl_resource* keyboard) {
    if (host->keymapText.empty()) return;
    int fd = memfd_create("wowdecomp-keymap", MFD_CLOEXEC);
    if (fd < 0) return;
    const char* text = host->keymapText.c_str();
    size_t len = host->keymapText.size();
    if (ftruncate(fd, (off_t)len) == 0) {
        ssize_t wrote = write(fd, text, len);
        if (wrote == (ssize_t)len) wl_keyboard_send_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)len);
    }
    close(fd);
}

void KeyboardRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
const struct wl_keyboard_interface kKeyboardImpl = { KeyboardRelease };

void PointerSetCursor(wl_client*, wl_resource*, uint32_t, wl_resource*, int32_t, int32_t) {}
void PointerRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
const struct wl_pointer_interface kPointerImpl = { PointerSetCursor, PointerRelease };

void SeatGetPointer(wl_client* client, wl_resource* resource, uint32_t id) {
    ClientState* state = StateFor(client);
    uint32_t version = wl_resource_get_version(resource);
    wl_resource* pointer = wl_resource_create(client, &wl_pointer_interface, version, id);
    if (!pointer) return;
    wl_resource_set_implementation(pointer, &kPointerImpl, state, nullptr);
    if (state) state->pointer = pointer;
}

void SeatGetKeyboard(wl_client* client, wl_resource* resource, uint32_t id) {
    ClientState* state = StateFor(client);
    uint32_t version = wl_resource_get_version(resource);
    wl_resource* keyboard = wl_resource_create(client, &wl_keyboard_interface, version, id);
    if (!keyboard) return;
    wl_resource_set_implementation(keyboard, &kKeyboardImpl, state, nullptr);
    if (!state) return;
    state->keyboard = keyboard;
    SendKeymap(state->host, keyboard);
}

void TouchRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
const struct wl_touch_interface kTouchImpl = { TouchRelease };

void SeatGetTouch(wl_client* client, wl_resource* resource, uint32_t id) {
    wl_resource* touch = wl_resource_create(client, &wl_touch_interface, wl_resource_get_version(resource), id);
    if (touch) wl_resource_set_implementation(touch, &kTouchImpl, nullptr, nullptr);
}

void SeatRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }

const struct wl_seat_interface kSeatImpl = { SeatGetPointer, SeatGetKeyboard, SeatGetTouch, SeatRelease };

void BindSeat(wl_client* client, void*, uint32_t version, uint32_t id) {
    if (version > 7) version = 7;
    wl_resource* seat = wl_resource_create(client, &wl_seat_interface, version, id);
    if (!seat) return;
    wl_resource_set_implementation(seat, &kSeatImpl, nullptr, nullptr);
    wl_seat_send_capabilities(seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (version >= 2) wl_seat_send_name(seat, "wowdecomp");
}

void SubsurfaceDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
void SubsurfacePosition(wl_client*, wl_resource* resource, int32_t x, int32_t y) {
    Surface* surface = SurfaceOf(resource);
    if (!surface) return;
    surface->x = x;
    surface->y = y;
}
void SubsurfaceAbove(wl_client*, wl_resource*, wl_resource*) {}
void SubsurfaceBelow(wl_client*, wl_resource*, wl_resource*) {}
void SubsurfaceSync(wl_client*, wl_resource*) {}
void SubsurfaceDesync(wl_client*, wl_resource*) {}

const struct wl_subsurface_interface kSubsurfaceImpl = {
    SubsurfaceDestroy, SubsurfacePosition, SubsurfaceAbove, SubsurfaceBelow, SubsurfaceSync, SubsurfaceDesync,
};

void SubcompositorDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }

void SubcompositorGet(wl_client* client, wl_resource*, uint32_t id, wl_resource* subsurface, wl_resource*) {
    Surface* surface = SurfaceOf(subsurface);
    wl_resource* resource = wl_resource_create(client, &wl_subsurface_interface, 1, id);
    if (!resource) return;
    wl_resource_set_implementation(resource, &kSubsurfaceImpl, surface, nullptr);
}

const struct wl_subcompositor_interface kSubcompositorImpl = { SubcompositorDestroy, SubcompositorGet };

void BindSubcompositor(wl_client* client, void*, uint32_t, uint32_t id) {
    wl_resource* resource = wl_resource_create(client, &wl_subcompositor_interface, 1, id);
    if (resource) wl_resource_set_implementation(resource, &kSubcompositorImpl, nullptr, nullptr);
}

void BindCompositor(wl_client* client, void*, uint32_t version, uint32_t id) {
    if (version > 4) version = 4;
    wl_resource* resource = wl_resource_create(client, &wl_compositor_interface, version, id);
    if (resource) wl_resource_set_implementation(resource, &kCompositorImpl, nullptr, nullptr);
}

void XdgDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
void XdgPong(wl_client*, wl_resource*, uint32_t) {}
void XdgSetSize(wl_client*, wl_resource*, int32_t, int32_t) {}
void XdgAnchorRect(wl_client*, wl_resource*, int32_t, int32_t, int32_t, int32_t) {}
void XdgAnchor(wl_client*, wl_resource*, uint32_t) {}
void XdgGravity(wl_client*, wl_resource*, uint32_t) {}
void XdgConstraint(wl_client*, wl_resource*, uint32_t) {}
void XdgOffset(wl_client*, wl_resource*, int32_t, int32_t) {}
void XdgReactive(wl_client*, wl_resource*) {}
void XdgParentSize(wl_client*, wl_resource*, int32_t, int32_t) {}
void XdgParentConfigure(wl_client*, wl_resource*, uint32_t) {}

const struct xdg_positioner_interface kPositionerImpl = {
    XdgDestroy, XdgSetSize, XdgAnchorRect, XdgAnchor, XdgGravity, XdgConstraint, XdgOffset, XdgReactive, XdgParentSize, XdgParentConfigure,
};

void XdgCreatePositioner(wl_client* client, wl_resource*, uint32_t id) {
    wl_resource* resource = wl_resource_create(client, &xdg_positioner_interface, 1, id);
    if (resource) wl_resource_set_implementation(resource, &kPositionerImpl, nullptr, nullptr);
}

struct XdgRole {
    wl_resource* surface = nullptr;
    wl_resource* xdg = nullptr;
    wl_resource* toplevel = nullptr;
    Host* host = nullptr;
};

void Configure(XdgRole* role) {
    if (!role || !role->xdg || !role->toplevel || !role->host) return;
    wl_array states;
    wl_array_init(&states);
    uint32_t* state = static_cast<uint32_t*>(wl_array_add(&states, sizeof(uint32_t)));
    if (state) *state = XDG_TOPLEVEL_STATE_ACTIVATED;
    xdg_toplevel_send_configure(role->toplevel, role->host->reqW, role->host->reqH, &states);
    wl_array_release(&states);
    xdg_surface_send_configure(role->xdg, NextSerial(role->host));
}

void XdgToplevelDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
void XdgSetParent(wl_client*, wl_resource*, wl_resource*) {}
void XdgSetTitle(wl_client*, wl_resource* resource, const char* title) {
    auto* role = static_cast<XdgRole*>(wl_resource_get_user_data(resource));
    if (role && role->host && title) std::snprintf(role->host->title, sizeof(role->host->title), "%s", title);
}
void XdgSetApp(wl_client*, wl_resource*, const char*) {}
void XdgMenu(wl_client*, wl_resource*, wl_resource*, uint32_t, int32_t, int32_t) {}
void XdgMove(wl_client*, wl_resource*, wl_resource*, uint32_t) {}
void XdgResize(wl_client*, wl_resource*, wl_resource*, uint32_t, uint32_t) {}
void XdgMax(wl_client*, wl_resource*, int32_t, int32_t) {}
void XdgMaximize(wl_client*, wl_resource*) {}
void XdgUnmaximize(wl_client*, wl_resource*) {}
void XdgFullscreen(wl_client*, wl_resource*, wl_resource*) {}
void XdgUnfullscreen(wl_client*, wl_resource*) {}
void XdgMinimize(wl_client*, wl_resource*) {}

const struct xdg_toplevel_interface kToplevelImpl = {
    XdgToplevelDestroy, XdgSetParent, XdgSetTitle, XdgSetApp, XdgMenu, XdgMove, XdgResize, XdgMax, XdgMax,
    XdgMaximize, XdgUnmaximize, XdgFullscreen, XdgUnfullscreen, XdgMinimize,
};

void XdgGetToplevel(wl_client* client, wl_resource* resource, uint32_t id) {
    auto* role = static_cast<XdgRole*>(wl_resource_get_user_data(resource));
    wl_resource* toplevel = wl_resource_create(client, &xdg_toplevel_interface, 1, id);
    if (!toplevel || !role) return;
    role->toplevel = toplevel;
    wl_resource_set_implementation(toplevel, &kToplevelImpl, role, nullptr);
    if (role->host) {
        for (ClientState* state : role->host->clients) {
            if (state->client == client) state->toplevelSurface = role->surface;
        }
    }
    Configure(role);
}

void XdgGetPopup(wl_client*, wl_resource*, uint32_t, wl_resource*, wl_resource*) {}
void XdgGeometry(wl_client*, wl_resource* resource, int32_t x, int32_t y, int32_t width, int32_t height) {
    auto* role = static_cast<XdgRole*>(wl_resource_get_user_data(resource));
    Surface* surface = role && role->surface ? SurfaceOf(role->surface) : nullptr;
    if (!surface) return;
    surface->geoX = x;
    surface->geoY = y;
    surface->geoW = width;
    surface->geoH = height;
}
void XdgAck(wl_client*, wl_resource*, uint32_t) {}

const struct xdg_surface_interface kXdgSurfaceImpl = { XdgDestroy, XdgGetToplevel, XdgGetPopup, XdgGeometry, XdgAck };

void XdgGetSurface(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* surface) {
    Host* host = static_cast<Host*>(wl_resource_get_user_data(resource));
    auto* role = new XdgRole();
    role->surface = surface;
    role->host = host;
    role->xdg = wl_resource_create(client, &xdg_surface_interface, 1, id);
    if (!role->xdg) {
        delete role;
        return;
    }
    if (host) host->roles.push_back(role);
    wl_resource_set_implementation(role->xdg, &kXdgSurfaceImpl, role, [](wl_resource* res) {
        auto* gone = static_cast<XdgRole*>(wl_resource_get_user_data(res));
        if (gone && gone->host) {
            auto& list = gone->host->roles;
            for (auto it = list.begin(); it != list.end(); ++it) {
                if (*it == gone) {
                    list.erase(it);
                    break;
                }
            }
        }
        delete gone;
    });
}

const struct xdg_wm_base_interface kWmImpl = { XdgDestroy, XdgCreatePositioner, XdgGetSurface, XdgPong };

void BindWm(wl_client* client, void* data, uint32_t version, uint32_t id) {
    if (version > 3) version = 3;
    Host* host = static_cast<Host*>(data);
    wl_resource* wm = wl_resource_create(client, &xdg_wm_base_interface, version, id);
    if (!wm) return;
    host->wm = wm;
    wl_resource_set_implementation(wm, &kWmImpl, host, nullptr);
}

void DecorationDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }

void DecorationSendServer(wl_resource* resource) {
    zxdg_toplevel_decoration_v1_send_configure(resource, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void DecorationSetMode(wl_client*, wl_resource* resource, uint32_t) { DecorationSendServer(resource); }
void DecorationUnsetMode(wl_client*, wl_resource* resource) { DecorationSendServer(resource); }

const struct zxdg_toplevel_decoration_v1_interface kDecorationImpl = { DecorationDestroy, DecorationSetMode, DecorationUnsetMode };

// ʕ •ᴥ•ʔ✿ The game frame is the title bar, so clients are told not to draw their own ✿ ʕ •ᴥ•ʔ
void DecorationGet(wl_client* client, wl_resource* resource, uint32_t id, wl_resource*) {
    wl_resource* decoration = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface, wl_resource_get_version(resource), id);
    if (!decoration) return;
    wl_resource_set_implementation(decoration, &kDecorationImpl, nullptr, nullptr);
    DecorationSendServer(decoration);
}

const struct zxdg_decoration_manager_v1_interface kDecorationManagerImpl = { DecorationDestroy, DecorationGet };

void BindDecoration(wl_client* client, void* data, uint32_t version, uint32_t id) {
    wl_resource* manager = wl_resource_create(client, &zxdg_decoration_manager_v1_interface, version > 1 ? 1 : version, id);
    if (manager) wl_resource_set_implementation(manager, &kDecorationManagerImpl, data, nullptr);
}

void ClientGone(wl_listener* listener, void*) {
    auto* state = reinterpret_cast<ClientState*>(reinterpret_cast<char*>(listener) - offsetof(ClientState, gone));
    wl_list_remove(&listener->link);
    Host* host = state->host;
    for (auto it = host->clients.begin(); it != host->clients.end(); ++it) {
        if (*it == state) {
            host->clients.erase(it);
            break;
        }
    }
    delete state;
}

void ClientCreated(wl_listener* listener, void* data) {
    auto* hook = reinterpret_cast<decltype(Host::created)*>(reinterpret_cast<char*>(listener) - offsetof(decltype(Host::created), listener));
    Host* host = hook->self;
    auto* client = static_cast<wl_client*>(data);
    auto* state = new ClientState();
    state->host = host;
    state->client = client;
    state->gone.notify = ClientGone;
    wl_client_add_destroy_listener(client, &state->gone);
    host->clients.push_back(state);
}

int32_t EvdevForSym(Host* host, uint32_t sym) {
    if (!host->keymap) return 0;
    if (sym >= 'A' && sym <= 'Z') sym = sym - 'A' + 'a';
    for (uint32_t code = 8; code < 256; code++) {
        const xkb_keysym_t* syms = nullptr;
        int count = xkb_keymap_key_get_syms_by_level(host->keymap, code, 0, 0, &syms);
        if (count > 0 && syms && syms[0] == sym) return (int32_t)code;
    }
    return 0;
}

std::string FindOnPath(const std::string& name) {
    const char* path = getenv("PATH");
    std::string dirs = path && path[0] ? path : "/usr/local/bin:/usr/bin:/bin";
    size_t start = 0;
    while (start <= dirs.size()) {
        size_t end = dirs.find(':', start);
        if (end == std::string::npos) end = dirs.size();
        std::string dir = dirs.substr(start, end - start);
        if (!dir.empty()) {
            std::string full = dir + "/" + name;
            if (access(full.c_str(), X_OK) == 0) return full;
        }
        start = end + 1;
    }
    return name;
}

// ʕ •ᴥ•ʔ✿ Desktop Exec lines carry %f/%u style codes for the launcher to fill. Nothing is passed, so drop them ✿ ʕ •ᴥ•ʔ
std::string StripFieldCodes(const std::string& exec) {
    std::string out;
    for (size_t i = 0; i < exec.size(); i++) {
        if (exec[i] != '%' || i + 1 >= exec.size()) {
            out += exec[i];
            continue;
        }
        char code = exec[++i];
        if (code == '%') out += '%';
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void ScanDesktopDir(const std::string& dir, const std::string& prefix, std::vector<WaylandApp>& apps, std::vector<std::string>& seen) {
    DIR* handle = opendir(dir.c_str());
    if (!handle) return;
    while (dirent* entry = readdir(handle)) {
        std::string file = entry->d_name;
        if (file == "." || file == "..") continue;
        std::string path = dir + "/" + file;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
            ScanDesktopDir(path, prefix + file + "-", apps, seen);
            continue;
        }
        if (file.size() < 9 || file.compare(file.size() - 8, 8, ".desktop") != 0) continue;
        std::string id = prefix + file;
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) continue;
        seen.push_back(id);
        FILE* in = std::fopen(path.c_str(), "r");
        if (!in) continue;
        WaylandApp app;
        app.id = id;
        bool main = false;
        bool hidden = false;
        bool terminal = false;
        bool application = false;
        char line[2048];
        while (std::fgets(line, sizeof(line), in)) {
            std::string text = line;
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            if (!text.empty() && text[0] == '[') {
                main = text == "[Desktop Entry]";
                continue;
            }
            if (!main) continue;
            size_t eq = text.find('=');
            if (eq == std::string::npos) continue;
            std::string key = text.substr(0, eq);
            std::string value = text.substr(eq + 1);
            if (key == "Name") app.name = value;
            else if (key == "Comment") app.comment = value;
            else if (key == "Exec") app.exec = StripFieldCodes(value);
            else if (key == "Type") application = value == "Application";
            else if (key == "NoDisplay" || key == "Hidden") hidden = hidden || value == "true";
            else if (key == "Terminal") terminal = value == "true";
        }
        std::fclose(in);
        if (hidden || !application || app.name.empty() || app.exec.empty()) continue;
        if (terminal) {
            std::string kitty = FindOnPath("kitty");
            if (kitty == "kitty") continue;
            app.exec = kitty + " " + app.exec;
        }
        apps.push_back(app);
    }
    closedir(handle);
}

bool SplitApp(const std::string& text, std::vector<std::string>& args) {
    std::string cur;
    bool quote = false;
    for (char c : text) {
        if (c == '"') {
            quote = !quote;
            continue;
        }
        if (!quote && (c == ' ' || c == '\t')) {
            if (!cur.empty()) {
                args.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty()) args.push_back(cur);
    return !args.empty();
}

bool Spawn(Host* host, const std::vector<std::string>& args, char* error, uint32_t errorLen, bool waylandOnly) {
    std::vector<std::string> storage;
    std::vector<char*> argv;
    for (const std::string& arg : args) {
        storage.push_back(arg);
        argv.push_back(storage.back().data());
    }
    argv.push_back(nullptr);
    std::vector<std::string> envStore;
    bool replaced = false;
    std::string display = "WAYLAND_DISPLAY=" + host->socket;
    for (char** env = environ; env && *env; env++) {
        if (std::strncmp(*env, "WAYLAND_DISPLAY=", 16) == 0) {
            envStore.push_back(display);
            replaced = true;
        } else if (std::strncmp(*env, "WAYLAND_SOCKET=", 15) == 0) {
            continue;
        } else if (waylandOnly && std::strncmp(*env, "DISPLAY=", 8) == 0) {
            continue;
        } else {
            envStore.push_back(*env);
        }
    }
    if (!replaced) envStore.push_back(display);
    // ʕ •ᴥ•ʔ✿ This compositor only takes wl_shm. Mesa wants dmabuf unless told to copy, and RADV refuses to import the shm pages ✿ ʕ •ᴥ•ʔ
    bool wsi = false;
    for (std::string& entry : envStore) {
        if (entry.rfind("MESA_VK_WSI_DEBUG=", 0) == 0) {
            if (entry.find("sw") == std::string::npos) entry += entry.size() > 18 ? ",sw" : "sw";
            if (entry.find("noshm") == std::string::npos) entry += ",noshm";
            wsi = true;
        }
    }
    if (!wsi) envStore.push_back("MESA_VK_WSI_DEBUG=sw,noshm");
    std::vector<char*> envp;
    for (std::string& entry : envStore) envp.push_back(entry.data());
    envp.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    std::string logPath = "/tmp/" + host->socket + ".log";
    int logfd = open(logPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
    if (logfd >= 0) posix_spawn_file_actions_adddup2(&actions, logfd, STDERR_FILENO);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), envp.data());
    if (logfd >= 0) close(logfd);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        Note(error, errorLen, "could not start the wayland client");
        return false;
    }
    host->pid = pid;
    return true;
}

bool Launch(Host* host, const char* spec, char* error, uint32_t errorLen) {
    if (!spec || !spec[0]) {
        Note(error, errorLen, "missing wayland target");
        return false;
    }
    std::string text = spec;
    std::vector<std::string> args;
    if (text.rfind("https://", 0) == 0 || text.rfind("http://", 0) == 0 || text.rfind("html:", 0) == 0) {
        std::string url = text.rfind("html:", 0) == 0 ? text.substr(5) : text;
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
            Note(error, errorLen, "html targets must be http or https");
            return false;
        }
        const char* bin = "/usr/bin/chromium";
        if (access(bin, X_OK) != 0) bin = "/usr/bin/chromium-browser";
        if (access(bin, X_OK) != 0) {
            Note(error, errorLen, "chromium is not installed");
            return false;
        }
        std::string dir = "/tmp/wowdecomp-chrome-" + std::to_string(host->id);
        mkdir(dir.c_str(), 0700);
        const char* prefs = "{\"distribution\":{\"require_eula\":false,\"skip_first_run_ui\":true,\"suppress_first_run_default_browser_prompt\":true}}\n";
        auto plant = [&](const std::string& path, const char* body) {
            FILE* file = std::fopen(path.c_str(), "w");
            if (!file) return;
            if (body) std::fputs(body, file);
            std::fclose(file);
        };
        plant(dir + "/initial_preferences", prefs);
        plant(dir + "/master_preferences", prefs);
        plant(dir + "/EULA Accepted", nullptr);
        std::string size = "--window-size=" + std::to_string(host->reqW) + "," + std::to_string(host->reqH);
        std::string app = "--app=" + url;
        std::string data = "--user-data-dir=" + dir;
        args = { bin, "--ozone-platform=wayland", "--disable-gpu", "--disable-gpu-compositing", "--disable-dev-shm-usage",
            "--no-first-run", "--no-default-browser-check", data, size, app };
        std::snprintf(host->title, sizeof(host->title), "%s", url.c_str());
    } else {
        std::string command = text.rfind("app:", 0) == 0 ? text.substr(4) : text;
        if (!SplitApp(command, args)) {
            Note(error, errorLen, "missing application");
            return false;
        }
        if (args[0].find('/') == std::string::npos) args[0] = FindOnPath(args[0]);
        if (access(args[0].c_str(), X_OK) != 0) {
            Note(error, errorLen, "application is not executable");
            return false;
        }
        std::snprintf(host->title, sizeof(host->title), "%s", args[0].c_str());
    }
    bool web = text.rfind("https://", 0) == 0 || text.rfind("http://", 0) == 0 || text.rfind("html:", 0) == 0;
    return Spawn(host, args, error, errorLen, web);
}

void SendPointerFrame(ClientState* state) {
    if (state->pointer && wl_resource_get_version(state->pointer) >= 5) wl_pointer_send_frame(state->pointer);
}

void KeyToClients(Host* host, uint32_t keysym, int32_t down) {
    int32_t code = EvdevForSym(host, keysym);
    if (!code || !host->xkbState) return;
    xkb_state_update_key(host->xkbState, (uint32_t)code, down ? XKB_KEY_DOWN : XKB_KEY_UP);
    uint32_t depressed = xkb_state_serialize_mods(host->xkbState, XKB_STATE_MODS_DEPRESSED);
    uint32_t latched = xkb_state_serialize_mods(host->xkbState, XKB_STATE_MODS_LATCHED);
    uint32_t locked = xkb_state_serialize_mods(host->xkbState, XKB_STATE_MODS_LOCKED);
    uint32_t group = xkb_state_serialize_layout(host->xkbState, XKB_STATE_LAYOUT_EFFECTIVE);
    for (ClientState* state : host->clients) {
        if (!state->keyboard) continue;
        uint32_t serial = NextSerial(host);
        wl_keyboard_send_modifiers(state->keyboard, serial, depressed, latched, locked, group);
        // ʕ •ᴥ•ʔ✿ wl_keyboard.key takes the evdev code, which sits 8 below the XKB keycode ✿ ʕ •ᴥ•ʔ
        wl_keyboard_send_key(state->keyboard, serial, NowMs(), (uint32_t)code - 8, down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

}  // namespace

int32_t WaylandEmbedAvailable() { return 1; }

const std::vector<WaylandApp>& WaylandEmbedApps(int32_t rescan) {
    static std::vector<WaylandApp> apps;
    static bool scanned = false;
    if (scanned && !rescan) return apps;
    scanned = true;
    apps.clear();
    std::vector<std::string> seen;
    // ʕ •ᴥ•ʔ✿ XDG order: the user's data home shadows the system dirs, first id wins ✿ ʕ •ᴥ•ʔ
    std::vector<std::string> roots;
    const char* home = getenv("XDG_DATA_HOME");
    if (home && home[0]) roots.push_back(home);
    else if (const char* user = getenv("HOME")) roots.push_back(std::string(user) + "/.local/share");
    const char* dirs = getenv("XDG_DATA_DIRS");
    std::string list = dirs && dirs[0] ? dirs : "/usr/local/share:/usr/share";
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(':', start);
        if (end == std::string::npos) end = list.size();
        if (end > start) roots.push_back(list.substr(start, end - start));
        start = end + 1;
    }
    for (const std::string& root : roots) ScanDesktopDir(root + "/applications", "", apps, seen);
    std::sort(apps.begin(), apps.end(), [](const WaylandApp& a, const WaylandApp& b) { return a.name < b.name; });
    return apps;
}

int32_t WaylandEmbedOpen(const char* spec, int32_t width, int32_t height, char* error, uint32_t errorLen) {
    if (!getenv("XDG_RUNTIME_DIR")) {
        Note(error, errorLen, "XDG_RUNTIME_DIR is not set");
        return 0;
    }
    auto* host = new Host();
    host->id = g_nextId++;
    if (width < 160) width = 160;
    if (height < 120) height = 120;
    if (width > kMaxEdge) width = kMaxEdge;
    if (height > kMaxEdge) height = kMaxEdge;
    host->reqW = width;
    host->reqH = height;
    host->display = wl_display_create();
    if (!host->display || wl_display_init_shm(host->display) != 0) {
        if (host->display) wl_display_destroy(host->display);
        delete host;
        Note(error, errorLen, "wayland display failed");
        return 0;
    }
    host->socket = "wowdecomp-" + std::to_string(host->id);
    if (wl_display_add_socket(host->display, host->socket.c_str()) != 0) {
        wl_display_destroy(host->display);
        delete host;
        Note(error, errorLen, "wayland socket is in use");
        return 0;
    }
    host->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (host->xkb) {
        host->keymap = xkb_keymap_new_from_names(host->xkb, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (host->keymap) {
            host->xkbState = xkb_state_new(host->keymap);
            char* text = xkb_keymap_get_as_string(host->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
            if (text) {
                host->keymapText = text;
                free(text);
            }
        }
    }
    wl_global_create(host->display, &wl_compositor_interface, 4, host, BindCompositor);
    wl_global_create(host->display, &wl_output_interface, 2, host, BindOutput);
    wl_global_create(host->display, &wl_seat_interface, 7, host, BindSeat);
    wl_global_create(host->display, &wl_subcompositor_interface, 1, host, BindSubcompositor);
    wl_global_create(host->display, &xdg_wm_base_interface, 3, host, BindWm);
    wl_global_create(host->display, &zxdg_decoration_manager_v1_interface, 1, host, BindDecoration);
    host->created.self = host;
    host->created.listener.notify = ClientCreated;
    wl_display_add_client_created_listener(host->display, &host->created.listener);
    if (!Launch(host, spec, error, errorLen)) {
        wl_display_destroy(host->display);
        if (host->xkbState) xkb_state_unref(host->xkbState);
        if (host->keymap) xkb_keymap_unref(host->keymap);
        if (host->xkb) xkb_context_unref(host->xkb);
        delete host;
        return 0;
    }
    g_hosts.push_back(host);
    return host->id;
}

void WaylandEmbedClose(int32_t id) {
    Host* host = FindHost(id);
    if (!host) return;
    if (g_focus == id) g_focus = 0;
    if (host->pid > 0) {
        kill(host->pid, SIGTERM);
        int status = 0;
        waitpid(host->pid, &status, WNOHANG);
    }
    wl_list_remove(&host->created.listener.link);
    wl_display_destroy_clients(host->display);
    wl_display_destroy(host->display);
    if (host->xkbState) xkb_state_unref(host->xkbState);
    if (host->keymap) xkb_keymap_unref(host->keymap);
    if (host->xkb) xkb_context_unref(host->xkb);
    for (auto it = g_hosts.begin(); it != g_hosts.end(); ++it) {
        if (*it == host) {
            g_hosts.erase(it);
            break;
        }
    }
    delete host;
}

void WaylandEmbedService() {
    for (Host* host : g_hosts) {
        wl_event_loop* loop = wl_display_get_event_loop(host->display);
        wl_event_loop_dispatch(loop, 0);
        wl_display_flush_clients(host->display);
        if (host->pid > 0) {
            int status = 0;
            pid_t done = waitpid(host->pid, &status, WNOHANG);
            if (done == host->pid) host->pid = -1;
        }
    }
}

int32_t WaylandEmbedPixels(int32_t id, WaylandPixels* out) {
    Host* host = FindHost(id);
    if (!host || !out || host->frame.empty()) return 0;
    out->data = host->frame.data();
    out->width = host->width;
    out->height = host->height;
    out->stride = host->stride;
    out->serial = host->stamp;
    return 1;
}

void WaylandEmbedScroll(int32_t id, int32_t delta) {
    Host* host = FindHost(id);
    if (!host || !delta) return;
    for (ClientState* state : host->clients) {
        if (!state->pointer || !state->toplevelSurface) continue;
        uint32_t version = wl_resource_get_version(state->pointer);
        if (version >= 5) {
            wl_pointer_send_axis_source(state->pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
            wl_pointer_send_axis_discrete(state->pointer, WL_POINTER_AXIS_VERTICAL_SCROLL, -delta);
        }
        wl_pointer_send_axis(state->pointer, NowMs(), WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(-delta * 40));
        if (version >= 5) wl_pointer_send_axis_stop(state->pointer, NowMs(), WL_POINTER_AXIS_VERTICAL_SCROLL);
        SendPointerFrame(state);
    }
}

void WaylandEmbedPointer(int32_t id, int32_t x, int32_t y, int32_t button, int32_t down) {
    Host* host = FindHost(id);
    if (!host) return;
    for (ClientState* state : host->clients) {
        if (!state->pointer || !state->toplevelSurface) continue;
        wl_fixed_t fx = wl_fixed_from_int(x + host->cropX);
        wl_fixed_t fy = wl_fixed_from_int(y + host->cropY);
        if (state->entered != SurfaceOf(state->toplevelSurface)) {
            wl_pointer_send_enter(state->pointer, NextSerial(host), state->toplevelSurface, fx, fy);
            state->entered = SurfaceOf(state->toplevelSurface);
        }
        wl_pointer_send_motion(state->pointer, NowMs(), fx, fy);
        if (button > 0) {
            uint32_t code = 0x110 + (uint32_t)(button - 1);
            wl_pointer_send_button(state->pointer, NextSerial(host), NowMs(), code, down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        }
        SendPointerFrame(state);
    }
}

void WaylandEmbedResize(int32_t id, int32_t width, int32_t height) {
    Host* host = FindHost(id);
    if (!host) return;
    if (width < 160) width = 160;
    if (height < 120) height = 120;
    if (width > kMaxEdge) width = kMaxEdge;
    if (height > kMaxEdge) height = kMaxEdge;
    if (width == host->reqW && height == host->reqH) return;
    host->reqW = width;
    host->reqH = height;
    for (XdgRole* role : host->roles) Configure(role);
}

void WaylandEmbedClearFocus() {
    if (g_focus) WaylandEmbedFocus(g_focus, 0);
}

void WaylandEmbedFocus(int32_t id, int32_t focused) {
    Host* host = FindHost(id);
    if (!host) return;
    host->focused = focused != 0;
    if (host->focused) g_focus = id;
    else if (g_focus == id) g_focus = 0;
    for (ClientState* state : host->clients) {
        if (!state->keyboard || !state->toplevelSurface) continue;
        if (host->focused && !state->keysIn) {
            wl_array empty;
            wl_array_init(&empty);
            wl_keyboard_send_enter(state->keyboard, NextSerial(host), state->toplevelSurface, &empty);
            wl_array_release(&empty);
            state->keysIn = true;
        } else if (!host->focused && state->keysIn) {
            wl_keyboard_send_leave(state->keyboard, NextSerial(host), state->toplevelSurface);
            state->keysIn = false;
        }
    }
}

void WaylandEmbedKeySym(int32_t id, uint32_t keysym, int32_t down) {
    Host* host = FindHost(id);
    if (!host || !host->focused) return;
    KeyToClients(host, keysym, down);
}

int32_t WaylandEmbedTakeKey(int32_t sdlSym, int32_t down) {
    if (!g_focus) return 0;
    uint32_t sym = (uint32_t)sdlSym;
    if (sdlSym == 13) sym = 0xff0d;
    else if (sdlSym == 27) sym = 0xff1b;
    else if (sdlSym == 8) sym = 0xff08;
    else if (sdlSym == 9) sym = 0xff09;
    else if (sdlSym == 127) sym = 0xffff;
    WaylandEmbedKeySym(g_focus, sym, down);
    return 1;
}

const char* WaylandEmbedTitle(int32_t id) {
    Host* host = FindHost(id);
    return host ? host->title : "";
}
