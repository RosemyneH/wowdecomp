#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <SDL.h>
#include <SDL_vulkan.h>
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Storm/H/storm.h"

static SDL_Window* s_GxDevWindow;
static SDL_Window* s_GxProcWindow;
static OsGuiWindowProc s_GxWindowProc;
static void* s_GxWindowParam;
static int32_t s_gxVisibleSent = 1;
static int32_t GxWindowVisible(SDL_Window* window);

// INLINED
int32_t COsDialog::OnEvent(int32_t itemId, int32_t notifyCode, int32_t code) {
    if (this->m_callback) {
        OsGuiCallbackParams params;
        params.type = itemId;
        params.subType = notifyCode;
        params.code = code;
        params.user = this->m_callbackParam;
        this->m_callback(params);
    }

    return 1;
}

// OFFSET: 0x0086C520
int32_t COsControl::SendEvent(int32_t event, int32_t code) {
    if (this->OnEvent(this->m_id, event, code)) {
        return 1;
    }

    if (this->m_dialog) {
        return this->m_dialog->OnEvent(this->m_id, event, code);
    }

    return 0;
}

// OFFSET: 0x0086C590
static int32_t sIsCharacterAllowed(char c, uint32_t filters) {
    uint8_t ch = static_cast<uint8_t>(c);

    if (ch <= 0x1F) {
        return 1;
    }

    if (filters & 0x1) {
        if (ch >= 'a' && ch <= 'z') {
            return 1;
        }

        if (ch >= 'A' && ch <= 'Z') {
            return 1;
        }
    }

    if ((filters & 0x2) && ch >= '0' && ch <= '9') {
        return 1;
    }

    if ((filters & 0x4) && (ch == '+' || ch == '-')) {
        return 1;
    }

    if ((filters & 0x8) && ch == '.') {
        return 1;
    }

    if ((filters & 0x10) && ch == ' ') {
        return 1;
    }

    if ((filters & 0x20) && ch == '_') {
        return 1;
    }

    return (filters & 0x40) && ch <= 0x7E;
}

void* COsTreeView::GetEditControl() {
    return nullptr;
}

// OFFSET: 0x0086C600
int32_t COsTreeView::IsCharacterAllowed(char c) {
    if (!this->GetEditControl()) {
        return 1;
    }

    if (this->m_filtersEnabled) {
        return sIsCharacterAllowed(c, this->m_filters);
    }

    return 1;
}

void OsGuiSetWindowTitle(void* window, const char* title) {
    if (window) {
        SDL_SetWindowTitle(static_cast<SDL_Window*>(window), title);
    }
}

void* OsGuiGetWindow(int32_t type) {
    switch (type) {
        case 0:
            return s_GxDevWindow;

        case 1:
        case 2:
            return SDL_GetKeyboardFocus();

        default:
            return nullptr;
    }
}

// OFFSET: 0x0086C6D0
void OsGuiSetGxWindow(void* window) {
    s_GxDevWindow = static_cast<SDL_Window*>(window);
}

uint32_t OsGuiGetWindowFlags() {
    // ʕ •ᴥ•ʔ✿ SDL refuses METAL|VULKAN together; the Vulkan flag already creates the Metal layer for MoltenVK ✿ ʕ •ᴥ•ʔ
    return SDL_WINDOW_ALLOW_HIGHDPI;
}

float OsGuiGetPixelScale(void* window) {
    if (!window) {
        return 1.0f;
    }

    int32_t pointWidth = 0;
    int32_t pointHeight = 0;
    int32_t pixelWidth = 0;
    int32_t pixelHeight = 0;

    SDL_GetWindowSize(static_cast<SDL_Window*>(window), &pointWidth, &pointHeight);
    SDL_Vulkan_GetDrawableSize(static_cast<SDL_Window*>(window), &pixelWidth, &pixelHeight);

    if (pointWidth <= 0 || pointHeight <= 0) {
        return 1.0f;
    }

    float scaleX = static_cast<float>(pixelWidth) / static_cast<float>(pointWidth);
    float scaleY = static_cast<float>(pixelHeight) / static_cast<float>(pointHeight);

    return scaleX > scaleY ? scaleX : scaleY;
}

static int GxWindowEventWatch(void* userdata, SDL_Event* event) {
    if (event->type != SDL_WINDOWEVENT || !s_GxWindowProc || SDL_GetWindowFromID(event->window.windowID) != s_GxProcWindow) {
        return 0;
    }

    int32_t width;
    int32_t height;
    OsGuiGetDrawableSize(s_GxProcWindow, &width, &height);

    switch (event->window.event) {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_MAXIMIZED:
        case SDL_WINDOWEVENT_SHOWN:
        case SDL_WINDOWEVENT_EXPOSED:
            s_gxVisibleSent = GxWindowVisible(s_GxProcWindow);
            s_GxWindowProc(s_GxWindowParam, s_gxVisibleSent ? OS_GUI_WM_SIZE : OS_GUI_WM_MINIMIZE, width, height);
            break;

        case SDL_WINDOWEVENT_MINIMIZED:
        case SDL_WINDOWEVENT_HIDDEN:
            s_gxVisibleSent = 0;
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_MINIMIZE, width, height);
            break;

        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_DISPLAYCHANGE, width, height);
            break;

        case SDL_WINDOWEVENT_FOCUS_GAINED:
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_SETFOCUS, width, height);
            break;

        case SDL_WINDOWEVENT_FOCUS_LOST:
            s_GxWindowProc(s_GxWindowParam, OS_GUI_WM_KILLFOCUS, width, height);
            break;

        default:
            break;
    }

    return 0;
}

// ʕ ◕ᴥ◕ ʔ✿ SDL3 keeps an occluded flag that sdl2-compat never surfaces as an event ✿ ʕ ◕ᴥ◕ ʔ
using Sdl3GetWindowFlags = uint64_t (*)(SDL_Window*);
static Sdl3GetWindowFlags s_sdl3GetWindowFlags = nullptr;
static int32_t s_sdl3Probed = 0;

static void* OpenLoadedSdl3() {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char* name = _dyld_get_image_name(i);
        if (!name || (!strstr(name, "libSDL3") && !strstr(name, "SDL3.framework"))) continue;
        void* lib = dlopen(name, RTLD_NOLOAD | RTLD_LAZY);
        if (lib) return lib;
    }

    const char* names[] = { "libSDL3.dylib", "libSDL3.0.dylib" };
    for (uint32_t i = 0; i < 2; i++) {
        void* lib = dlopen(names[i], RTLD_NOLOAD | RTLD_LAZY);
        if (lib) return lib;
    }

    return nullptr;
}

static int32_t GxWindowOccluded(SDL_Window* window) {
    if (!window) return 0;
    if (!s_sdl3GetWindowFlags && !s_sdl3Probed) {
        void* lib = OpenLoadedSdl3();
        if (lib) s_sdl3GetWindowFlags = reinterpret_cast<Sdl3GetWindowFlags>(dlsym(lib, "SDL_GetWindowFlags"));
        if (s_sdl3GetWindowFlags || SDL_WasInit(SDL_INIT_VIDEO)) s_sdl3Probed = 1;
    }
    // ʕ •ᴥ•ʔ✿ Bit 0x4 is SDL_WINDOW_OCCLUDED in SDL3 and SDL_WINDOW_SHOWN in SDL2 ✿ ʕ •ᴥ•ʔ
    return s_sdl3GetWindowFlags && (s_sdl3GetWindowFlags(window) & 0x4) != 0;
}

static int32_t GxWindowVisible(SDL_Window* window) {
    if (!window) return 0;
    Uint32 flags = SDL_GetWindowFlags(window);
    if ((flags & SDL_WINDOW_MINIMIZED) || (flags & SDL_WINDOW_HIDDEN) || GxWindowOccluded(window)) return 0;
    int32_t width, height;
    OsGuiGetDrawableSize(window, &width, &height);
    return width > 0 && height > 0;
}

void OsGuiPollGxWindowState() {
    if (!s_GxWindowProc || !s_GxProcWindow) return;
    int32_t visible = GxWindowVisible(s_GxProcWindow);
    if (visible == s_gxVisibleSent) return;
    s_gxVisibleSent = visible;
    int32_t width, height;
    OsGuiGetDrawableSize(s_GxProcWindow, &width, &height);
    s_GxWindowProc(s_GxWindowParam, visible ? OS_GUI_WM_SIZE : OS_GUI_WM_MINIMIZE, width, height);
}

void OsGuiSetGxWindowProc(void* window, OsGuiWindowProc proc, void* param) {
    s_gxVisibleSent = 1;
    if (s_GxWindowProc) {
        SDL_DelEventWatch(GxWindowEventWatch, nullptr);
    }

    s_GxProcWindow = static_cast<SDL_Window*>(window);
    s_GxWindowProc = proc;
    s_GxWindowParam = param;

    if (proc) {
        SDL_AddEventWatch(GxWindowEventWatch, nullptr);
    }
}

void* OsGuiCreateGxWindow(const char* title, int32_t width, int32_t height, int32_t fullscreen) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return nullptr;
    }

    uint32_t flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | OsGuiGetWindowFlags();

    if (fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    return SDL_CreateWindow(title ? title : "", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
}

void OsGuiDestroyGxWindow(void* window) {
    if (window == s_GxDevWindow) {
        s_GxDevWindow = nullptr;
    }

    if (window == s_GxProcWindow) {
        if (s_GxWindowProc) {
            SDL_DelEventWatch(GxWindowEventWatch, nullptr);
        }
        s_GxProcWindow = nullptr;
        s_GxWindowProc = nullptr;
        s_GxWindowParam = nullptr;
    }

    SDL_DestroyWindow(static_cast<SDL_Window*>(window));
}

void OsGuiGetDrawableSize(void* window, int32_t* width, int32_t* height) {
    int pixelWidth = 0;
    int pixelHeight = 0;

    if (window) {
        SDL_Vulkan_GetDrawableSize(static_cast<SDL_Window*>(window), &pixelWidth, &pixelHeight);
    }

    *width = pixelWidth;
    *height = pixelHeight;
}

int32_t OsGuiGetVulkanInstanceExtensions(void* window, uint32_t* count, const char** names) {
    unsigned int extensionCount = *count;

    if (!SDL_Vulkan_GetInstanceExtensions(static_cast<SDL_Window*>(window), &extensionCount, names)) {
        return 0;
    }

    *count = extensionCount;

    return 1;
}

int32_t OsGuiCreateVulkanSurface(void* window, void* instance, void* surface) {
    return SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(window), static_cast<VkInstance>(instance), static_cast<VkSurfaceKHR*>(surface)) == SDL_TRUE;
}

int32_t OsGuiMessageBox(void* parentWindow, int32_t style, const char* message, const char* title) {
    enum { RESULT_YES = 0, RESULT_NO = 1, RESULT_CANCEL = 2 };

    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return RESULT_CANCEL;
    }

    SDL_MessageBoxButtonData buttons[3];
    int32_t count = 0;

    switch (style) {
        case 1:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, RESULT_YES, "OK" };
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_CANCEL, "Cancel" };
            break;

        case 2:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, RESULT_YES, "Yes" };
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_NO, "No" };
            break;

        case 3:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, RESULT_YES, "Yes" };
            buttons[count++] = { 0, RESULT_NO, "No" };
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_CANCEL, "Cancel" };
            break;

        default:
            buttons[count++] = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, RESULT_YES, "OK" };
            break;
    }

    SDL_MessageBoxData data = {};
    data.flags = SDL_MESSAGEBOX_INFORMATION;
    data.window = static_cast<SDL_Window*>(parentWindow);
    data.title = title ? title : "";
    data.message = message;
    data.numbuttons = count;
    data.buttons = buttons;

    int pressed;
    if (SDL_ShowMessageBox(&data, &pressed) || pressed < 0) {
        return RESULT_CANCEL;
    }

    return pressed;
}

int32_t OsGuiIsModifierKeyDown(int32_t key) {
    SDL_Keymod mod = SDL_GetModState();

    switch (key) {
        case 0:
            return (mod & KMOD_CTRL) != 0;

        case 1:
            return (mod & KMOD_SHIFT) != 0;

        case 2:
            return (mod & KMOD_ALT) != 0;

        default:
            return 0;
    }
}

void OsGuiBeep() {
}

// INLINED
int32_t COsEditBox::IsCharacterAllowed(char c) {
    if (!this->m_filtersEnabled) {
        return 1;
    }

    return sIsCharacterAllowed(c, this->m_filters);
}

int32_t COsTabControl::GetNumItems() {
    return 0;
}

void* COsDialog::GetParentWindow() {
    return nullptr;
}

// INLINED
int32_t COsDialog::HasFlag(uint32_t flag) {
    return (this->m_flags & flag) != 0;
}

// INLINED
int32_t COsDialog::IsInFront() {
    return this->m_handle == OsGuiGetWindow(2);
}

// OFFSET: 0x0086C840
COsControl* COsDialog::FindControl(void* handle) {
    for (uint32_t i = 0; i < this->m_controls.Count(); i++) {
        if (this->m_controls[i]->IsHandleFromControl(handle)) {
            return this->m_controls[i];
        }
    }

    return nullptr;
}

// OFFSET: 0x0086C890
int32_t COsDialog::OnCancel() {
    if (!this->m_cancelButton || this->m_cancelButton->OnEvent(this->m_cancelButton->m_id, 0, 0)) {
        return 1;
    }

    return this->OnEvent(this->m_cancelButton->m_id, 0, 0);
}

// OFFSET: 0x0086C900
int32_t COsTabControl::OnControlTab() {
    int32_t shift = OsGuiIsModifierKeyDown(1);
    int32_t value = this->GetValue();
    int32_t count = this->GetNumItems();

    int32_t next = shift ? value - 1 : value + 1;

    if (next < 0) {
        next = count - 1;
    }

    if (next >= count) {
        next = 0;
    }

    this->SetValue(next);
    this->SendEvent(2, 0);

    return 1;
}

// OFFSET: 0x0086C980
int32_t COsDialog::OnControlTab() {
    int32_t count = this->m_controls.Count();

    for (int32_t i = 0; i < count; i++) {
        if (this->m_controls[i]->m_type == 15) {
            COsTabControl* tabControl = static_cast<COsTabControl*>(this->m_controls[i]);
            return tabControl ? tabControl->OnControlTab() : 0;
        }
    }

    return 0;
}

// ʕ •ᴥ•ʔ✿ No native dialogs or menu accelerators exist on macOS ✿ ʕ •ᴥ•ʔ
int32_t COsDialog::ProcessMessage(void* message) {
    return 0;
}

int32_t OsGuiProcessMessage(void* message) {
    return 0;
}

// ʕ •ᴥ•ʔ✿ volk must load through the same loader SDL used for the window ✿ ʕ •ᴥ•ʔ
void* OsGuiGetVulkanProcAddr() {
    return reinterpret_cast<void*>(SDL_Vulkan_GetVkGetInstanceProcAddr());
}
