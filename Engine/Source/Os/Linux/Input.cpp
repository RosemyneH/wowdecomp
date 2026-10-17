#include <cstdint>
#include <SDL3/SDL.h>
#include "Engine/Source/Event/Types.h"
#include "Engine/Source/Os/Gui.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Linux/SdlDisplay.h"
#include "Engine/Source/Os/WaylandEmbed.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C2iVector.h"

static int32_t s_appActive = 1;

static int32_t s_cursorHidden = -1;

static int32_t s_relResidueX;

static int32_t s_relResidueY;

static int32_t s_residueWidth;

static int32_t s_residueHeight;

static int32_t s_residuePixelWidth;

static int32_t s_residuePixelHeight;

static C2iVector s_mousePos;

static int32_t s_queueHead;

static int32_t s_queueTail;

static OSEVENT s_queue[16];

static int32_t s_mouseSpeed = 10;

static uint32_t s_osButtonState;

static tagRECT s_defaultWindowRect;

static int32_t s_windowSize;

static OS_MOUSE_MODE s_osMouseMode;

static int32_t s_WindowResizeLock;

static void (*s_eventPollProc)();

static int32_t s_quitEventsOnWindowDestroy;

static SDL_Window* GxWindow() {
    return static_cast<SDL_Window*>(OsGuiGetWindow(0));
}

static void GetPixelSize(SDL_Window* window, int32_t* width, int32_t* height);

static int32_t ScaleAxis(int32_t value, int32_t from, int32_t to, int32_t* residue) {
    int64_t scaled = static_cast<int64_t>(value) * to;

    if (residue) {
        scaled += *residue;
        int64_t whole = scaled / from;
        *residue = static_cast<int32_t>(scaled - whole * from);
        return static_cast<int32_t>(whole);
    }

    if (scaled >= 0) {
        scaled += from / 2;
    } else {
        scaled -= from / 2;
    }

    return static_cast<int32_t>(scaled / from);
}

// ʕ •ᴥ•ʔ✿ SDL reports window points; Gx and the UI work in drawable pixels (Retina, scaled Wayland) ✿ ʕ •ᴥ•ʔ
static void ScalePoint(int32_t* x, int32_t* y, int32_t toPixels, int32_t relative = 0) {
    SDL_Window* window = GxWindow();

    if (!window) {
        return;
    }

    int width;
    int height;
    int pixelWidth;
    int pixelHeight;
    SDL_GetWindowSize(window, &width, &height);
    GetPixelSize(window, &pixelWidth, &pixelHeight);

    if (width <= 0 || height <= 0 || pixelWidth <= 0 || pixelHeight <= 0 || (width == pixelWidth && height == pixelHeight)) {
        if (relative) {
            s_relResidueX = 0;
            s_relResidueY = 0;
        }
        return;
    }

    if (relative && (s_residueWidth != width || s_residueHeight != height || s_residuePixelWidth != pixelWidth || s_residuePixelHeight != pixelHeight)) {
        s_residueWidth = width;
        s_residueHeight = height;
        s_residuePixelWidth = pixelWidth;
        s_residuePixelHeight = pixelHeight;
        s_relResidueX = 0;
        s_relResidueY = 0;
    }

    if (toPixels) {
        *x = ScaleAxis(*x, width, pixelWidth, relative ? &s_relResidueX : nullptr);
        *y = ScaleAxis(*y, height, pixelHeight, relative ? &s_relResidueY : nullptr);
    } else {
        *x = ScaleAxis(*x, pixelWidth, width, nullptr);
        *y = ScaleAxis(*y, pixelHeight, height, nullptr);
    }
}

static void GetPixelSize(SDL_Window* window, int32_t* width, int32_t* height) {
    OsGuiGetDrawableSize(window, width, height);
}

// OFFSET: 0x00868D40
void OsQueuePut(OSINPUT id, int32_t param0, int32_t param1, int32_t param2, int32_t param3) {
    int32_t next = s_queueTail == 15 ? 0 : s_queueTail + 1;

    if (next == s_queueHead) {
        s_queueHead = s_queueHead == 15 ? 0 : s_queueHead + 1;
    }

    OSEVENT& event = s_queue[s_queueTail];
    event.id = id;
    event.param[0] = param0;
    event.param[1] = param1;
    event.param[2] = param2;
    event.param[3] = OsGetAsyncTimeMs();

    s_queueTail = next;
}

// OFFSET: 0x00868DB0
int32_t OsQueueGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    if (s_queueHead == s_queueTail) {
        return 0;
    }

    OSEVENT& event = s_queue[s_queueHead];
    *id = event.id;
    *param0 = event.param[0];
    *param1 = event.param[1];
    *param2 = event.param[2];
    *param3 = event.param[3];

    s_queueHead = s_queueHead == 15 ? 0 : s_queueHead + 1;

    return 1;
}

static int32_t ConvertKeyCode(SDL_Keycode code, KEY* key) {
    if (code >= SDLK_F1 && code <= SDLK_F12) {
        *key = static_cast<KEY>(KEY_F1 + (code - SDLK_F1));
        return 1;
    }

    if (code >= SDLK_KP_1 && code <= SDLK_KP_9) {
        *key = static_cast<KEY>(KEY_NUMPAD1 + (code - SDLK_KP_1));
        return 1;
    }

    switch (code) {
        case SDLK_LSHIFT: *key = KEY_LSHIFT; return 1;
        case SDLK_RSHIFT: *key = KEY_RSHIFT; return 1;
        case SDLK_LCTRL: *key = KEY_LCONTROL; return 1;
        case SDLK_RCTRL: *key = KEY_RCONTROL; return 1;
        case SDLK_LALT: *key = KEY_LALT; return 1;
        case SDLK_RALT: *key = KEY_RALT; return 1;
        case SDLK_ESCAPE: *key = KEY_ESCAPE; return 1;
        case SDLK_RETURN: *key = KEY_ENTER; return 1;
        case SDLK_KP_ENTER: *key = KEY_ENTER; return 1;
        case SDLK_BACKSPACE: *key = KEY_BACKSPACE; return 1;
        case SDLK_TAB: *key = KEY_TAB; return 1;
        case SDLK_LEFT: *key = KEY_LEFT; return 1;
        case SDLK_UP: *key = KEY_UP; return 1;
        case SDLK_RIGHT: *key = KEY_RIGHT; return 1;
        case SDLK_DOWN: *key = KEY_DOWN; return 1;
        case SDLK_INSERT: *key = KEY_INSERT; return 1;
        case SDLK_DELETE: *key = KEY_DELETE; return 1;
        case SDLK_HOME: *key = KEY_HOME; return 1;
        case SDLK_END: *key = KEY_END; return 1;
        case SDLK_PAGEUP: *key = KEY_PAGEUP; return 1;
        case SDLK_PAGEDOWN: *key = KEY_PAGEDOWN; return 1;
        case SDLK_CAPSLOCK: *key = KEY_CAPSLOCK; return 1;
        case SDLK_NUMLOCKCLEAR: *key = KEY_NUMLOCK; return 1;
        case SDLK_SCROLLLOCK: *key = KEY_SCROLLLOCK; return 1;
        case SDLK_PAUSE: *key = KEY_PAUSE; return 1;
        case SDLK_PRINTSCREEN: *key = KEY_PRINTSCREEN; return 1;
        case SDLK_AC_BACK: *key = KEY_ESCAPE; return 1;
        case SDLK_SPACE: *key = KEY_SPACE; return 1;
        case SDLK_KP_0: *key = KEY_NUMPAD0; return 1;
        case SDLK_KP_PLUS: *key = KEY_NUMPAD_PLUS; return 1;
        case SDLK_KP_MINUS: *key = KEY_NUMPAD_MINUS; return 1;
        case SDLK_KP_MULTIPLY: *key = KEY_NUMPAD_MULTIPLY; return 1;
        case SDLK_KP_DIVIDE: *key = KEY_NUMPAD_DIVIDE; return 1;
        case SDLK_KP_PERIOD: *key = KEY_NUMPAD_DECIMAL; return 1;
        default: break;
    }

    if (code >= SDLK_A && code <= SDLK_Z) {
        *key = static_cast<KEY>(code - SDLK_A + 'A');
        return 1;
    }

    if (code > 0x20 && code < 0x7F) {
        *key = static_cast<KEY>(code);
        return 1;
    }

    return 0;
}

static int32_t ConvertKeyToKeyCode(KEY key, SDL_Keycode* code) {
    if (key >= KEY_F1 && key <= KEY_F12) {
        *code = SDLK_F1 + (key - KEY_F1);
        return 1;
    }

    if (key >= KEY_NUMPAD1 && key <= KEY_NUMPAD9) {
        *code = SDLK_KP_1 + (key - KEY_NUMPAD1);
        return 1;
    }

    switch (key) {
        case KEY_LSHIFT: *code = SDLK_LSHIFT; return 1;
        case KEY_RSHIFT: *code = SDLK_RSHIFT; return 1;
        case KEY_LCONTROL: *code = SDLK_LCTRL; return 1;
        case KEY_RCONTROL: *code = SDLK_RCTRL; return 1;
        case KEY_LALT: *code = SDLK_LALT; return 1;
        case KEY_RALT: *code = SDLK_RALT; return 1;
        case KEY_ESCAPE: *code = SDLK_ESCAPE; return 1;
        case KEY_ENTER: *code = SDLK_RETURN; return 1;
        case KEY_BACKSPACE: *code = SDLK_BACKSPACE; return 1;
        case KEY_TAB: *code = SDLK_TAB; return 1;
        case KEY_LEFT: *code = SDLK_LEFT; return 1;
        case KEY_UP: *code = SDLK_UP; return 1;
        case KEY_RIGHT: *code = SDLK_RIGHT; return 1;
        case KEY_DOWN: *code = SDLK_DOWN; return 1;
        case KEY_INSERT: *code = SDLK_INSERT; return 1;
        case KEY_DELETE: *code = SDLK_DELETE; return 1;
        case KEY_HOME: *code = SDLK_HOME; return 1;
        case KEY_END: *code = SDLK_END; return 1;
        case KEY_PAGEUP: *code = SDLK_PAGEUP; return 1;
        case KEY_PAGEDOWN: *code = SDLK_PAGEDOWN; return 1;
        case KEY_CAPSLOCK: *code = SDLK_CAPSLOCK; return 1;
        case KEY_NUMLOCK: *code = SDLK_NUMLOCKCLEAR; return 1;
        case KEY_SCROLLLOCK: *code = SDLK_SCROLLLOCK; return 1;
        case KEY_PAUSE: *code = SDLK_PAUSE; return 1;
        case KEY_PRINTSCREEN: *code = SDLK_PRINTSCREEN; return 1;
        case KEY_SPACE: *code = SDLK_SPACE; return 1;
        case KEY_NUMPAD0: *code = SDLK_KP_0; return 1;
        case KEY_NUMPAD_PLUS: *code = SDLK_KP_PLUS; return 1;
        case KEY_NUMPAD_MINUS: *code = SDLK_KP_MINUS; return 1;
        case KEY_NUMPAD_MULTIPLY: *code = SDLK_KP_MULTIPLY; return 1;
        case KEY_NUMPAD_DIVIDE: *code = SDLK_KP_DIVIDE; return 1;
        case KEY_NUMPAD_DECIMAL: *code = SDLK_KP_PERIOD; return 1;
        default: break;
    }

    if (key >= KEY_A && key <= KEY_Z) {
        *code = SDLK_A + (key - KEY_A);
        return 1;
    }

    if (key > 0x20 && key < 0x7F) {
        *code = static_cast<SDL_Keycode>(key);
        return 1;
    }

    return 0;
}

static int32_t ConvertButton(uint8_t sdlButton, MOUSEBUTTON* button) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT: *button = MOUSE_BUTTON_LEFT; return 1;
        case SDL_BUTTON_MIDDLE: *button = MOUSE_BUTTON_MIDDLE; return 1;
        case SDL_BUTTON_RIGHT: *button = MOUSE_BUTTON_RIGHT; return 1;
        case SDL_BUTTON_X1: *button = MOUSE_BUTTON_XBUTTON1; return 1;
        case SDL_BUTTON_X2: *button = MOUSE_BUTTON_XBUTTON2; return 1;
        default: return 0;
    }
}

static void SaveMouse(int32_t x, int32_t y) {
    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        return;
    }

    s_mousePos.x = x;
    s_mousePos.y = y;
}

static void WarpMouse(int32_t x, int32_t y) {
    SDL_Window* window = GxWindow();
    if (!window) {
        return;
    }
    ScalePoint(&x, &y, 0);
    SDL_WarpMouseInWindow(window, x, y);
}

static void RestoreMouse() {
    WarpMouse(s_mousePos.x, s_mousePos.y);
}

static int32_t s_typedChar;

static void QueueText(const char* text) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(text);

    while (*p) {
        uint32_t ch = *p++;
        int32_t extra = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : ch >= 0xC0 ? 1 : 0;

        if (extra) {
            ch &= 0x3F >> extra;
        }

        for (; extra && (*p & 0xC0) == 0x80; extra--) {
            ch = (ch << 6) | (*p++ & 0x3F);
        }

        if (ch >= 0x20) {
            OsQueuePut(OS_INPUT_CHAR, static_cast<int32_t>(ch), 1, 0, 0);
        }
    }
}

// OFFSET: 0x00869620
void OsInputSetWindowResizeLock(int32_t resizeLock) {
    s_WindowResizeLock = resizeLock;
}

// OFFSET: 0x00869630
float OsInputGetMouseSpeed() {
    return s_mouseSpeed * 0.1f;
}

int32_t OsInputIsKeyDown(KEY key) {
    SDL_Keycode code;
    if (!ConvertKeyToKeyCode(key, &code)) {
        return 0;
    }

    int count;
    const bool* state = SDL_GetKeyboardState(&count);
    SDL_Scancode scancode = SDL_GetScancodeFromKey(code, nullptr);

    return scancode > SDL_SCANCODE_UNKNOWN && scancode < count && state[scancode];
}

int32_t OsInputIsMouseButtonDown(MOUSEBUTTON button) {
    SDL_MouseButtonFlags state = SDL_GetMouseState(nullptr, nullptr);

    switch (button) {
        case MOUSE_BUTTON_LEFT:
            return (state & SDL_BUTTON_LMASK) != 0;

        case MOUSE_BUTTON_MIDDLE:
            return (state & SDL_BUTTON_MMASK) != 0;

        case MOUSE_BUTTON_RIGHT:
            return (state & SDL_BUTTON_RMASK) != 0;

        case MOUSE_BUTTON_XBUTTON1:
            return (state & SDL_BUTTON_X1MASK) != 0;

        case MOUSE_BUTTON_XBUTTON2:
            return (state & SDL_BUTTON_X2MASK) != 0;

        default:
            return 0;
    }
}

static void EnsureTextInput(SDL_Window* window) {
#if defined(__ANDROID__)
    (void)window;
#else
    if (!window || SDL_TextInputActive(window)) return;
    SDL_StartTextInput(window);
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);
    SDL_Rect area = { 0, 0, width, height };
    SDL_SetTextInputArea(window, &area, 0);
#endif
}

void OsInputInitialize() {
    SdlUseWayland();
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_InitSubSystem(SDL_INIT_EVENTS);
    EnsureTextInput(GxWindow());
}

void OsInputDestroy() {
    if (SDL_Window* window = GxWindow()) {
        SDL_StopTextInput(window);
    }
    // ʕ •ᴥ•ʔ✿ ConsoleDeviceDestroy still calls SDL_DestroyWindow after this ✿ ʕ •ᴥ•ʔ
    if (OsGuiGetWindow(0)) {
        s_quitEventsOnWindowDestroy = 1;
        return;
    }
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

void OsInputQuitDeferredEvents() {
    if (!s_quitEventsOnWindowDestroy) {
        return;
    }
    s_quitEventsOnWindowDestroy = 0;
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

static void HandleMouseButton(const SDL_MouseButtonEvent& event, OSINPUT id) {
    MOUSEBUTTON button;
    if (!ConvertButton(event.button, &button)) {
        return;
    }

    if (id == OS_INPUT_MOUSE_DOWN) {
        if (!s_osButtonState) {
            SDL_CaptureMouse(true);
        }

        s_osButtonState |= button;
    } else {
        s_osButtonState &= ~button;

        if (!s_osButtonState) {
            SDL_CaptureMouse(false);
        }
    }

    int32_t x = event.x;
    int32_t y = event.y;

    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        x = s_mousePos.x;
        y = s_mousePos.y;
    } else {
        ScalePoint(&x, &y, 1);
    }

    OsQueuePut(id, button, x, y, 0);
}

// ʕ ◕ᴥ◕ ʔ✿ Vulkan draws its own cursor; hide the OS arrow only while the pointer is inside a focused window ✿ ʕ ◕ᴥ◕ ʔ
static void ApplyOsCursor(SDL_Window* window) {
    SDL_WindowFlags flags = window ? SDL_GetWindowFlags(window) : 0;
    int32_t focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    int32_t over = (flags & SDL_WINDOW_MOUSE_FOCUS) != 0;
    int32_t hide = focused && (over || s_osMouseMode == OS_MOUSE_MODE_RELATIVE);

    if (s_cursorHidden == hide) {
        return;
    }

    s_cursorHidden = hide;
    if (hide) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

static void HandleWindowEvent(const SDL_WindowEvent& event) {
    switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_EXPOSED:
            s_windowSize = 1;
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            s_appActive = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            s_osButtonState = 0;
            if (!s_appActive) SDL_CaptureMouse(false);
            else EnsureTextInput(GxWindow());
            OsQueuePut(OS_INPUT_FOCUS, s_appActive, 0, 0, 0);
            break;

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            OsQueuePut(OS_INPUT_CLOSE, 0, 0, 0, 0);
            break;

        default:
            break;
    }

    ApplyOsCursor(GxWindow());
}

// OFFSET: 0x00869D90
void OsInputSetEventPollProc(void (*pollProc)()) {
    s_eventPollProc = pollProc;
}

int32_t OsInputGet(OSINPUT* id, int32_t* param0, int32_t* param1, int32_t* param2, int32_t* param3) {
    *id = static_cast<OSINPUT>(-1);

    WaylandEmbedService();
    SDL_Window* window = GxWindow();
    EnsureTextInput(window);
    ApplyOsCursor(window);

    // ʕ ◕ᴥ◕ ʔ✿ Compare every call: Wayland scale changes may not arrive as SIZE_CHANGED ✿ ʕ ◕ᴥ◕ ʔ
    if (window && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {

        int32_t width;
        int32_t height;
        GetPixelSize(window, &width, &height);

        if (width != s_defaultWindowRect.right || height != s_defaultWindowRect.bottom) {
            s_defaultWindowRect.left = 0;
            s_defaultWindowRect.top = 0;
            s_defaultWindowRect.right = width;
            s_defaultWindowRect.bottom = height;

            *id = OS_INPUT_SIZE;
            *param0 = width;
            *param1 = height;
            *param2 = 0;
            *param3 = 0;

            return 1;
        }
    }

    if (s_queueHead == s_queueTail) {
        OsGuiPollGxWindowState();
        OsPollJoysticks(s_appActive);

        SDL_Event event;

        while (s_queueHead == s_queueTail) {
            if (!SDL_PollEvent(&event)) {
                return 0;
            }

            if (event.type == SDL_EVENT_QUIT) {
                *id = OS_INPUT_SHUTDOWN;
                return 1;
            }

            OsWindowProc(window, event.type, 0, reinterpret_cast<intptr_t>(&event));
        }
    }

    OsQueueGet(id, param0, param1, param2, param3);

    return 1;
}

void OsInputSetMouseMode(OS_MOUSE_MODE mode) {
    if (mode >= OS_MOUSE_MODES) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return;
    }

    if (mode == s_osMouseMode) {
        return;
    }

    if (mode == OS_MOUSE_MODE_RELATIVE) {
        s_osMouseMode = mode;
        SDL_SetWindowRelativeMouseMode(GxWindow(), true);
    } else if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        s_osMouseMode = OS_MOUSE_MODE_NORMAL;
        SDL_SetWindowRelativeMouseMode(GxWindow(), false);
        RestoreMouse();
    }

    ApplyOsCursor(GxWindow());
}

// OFFSET: 0x0086A070
void OsInputSetMouseSpeed(float speed) {
    float clamped = speed < 0.1f ? 0.1f : (speed >= 2.0f ? 2.0f : speed);
    s_mouseSpeed = static_cast<int32_t>(clamped * 10.0f);
}

void OsInputGetMousePosition(int32_t* x, int32_t* y) {
    int mouseX;
    int mouseY;

    if (s_osMouseMode == OS_MOUSE_MODE_RELATIVE) {
        mouseX = s_mousePos.x;
        mouseY = s_mousePos.y;
    } else {
        float pointX = 0.0f;
        float pointY = 0.0f;
        SDL_GetMouseState(&pointX, &pointY);

        mouseX = static_cast<int32_t>(pointX);
        mouseY = static_cast<int32_t>(pointY);
        ScalePoint(&mouseX, &mouseY, 1);
        SaveMouse(mouseX, mouseY);
    }

    if (x) {
        *x = mouseX;
    }

    if (y) {
        *y = mouseY;
    }
}

void OsInputSetMousePosition(int32_t x, int32_t y) {
    SaveMouse(x, y);
    WarpMouse(x, y);
}

int32_t OsGetDefaultWindowRect(tagRECT* rect) {
    if (!rect) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (!s_defaultWindowRect.right || !s_defaultWindowRect.bottom) {
        SDL_Window* window = GxWindow();

        if (!window) {
            return 0;
        }

        int32_t width;
        int32_t height;
        GetPixelSize(window, &width, &height);

        s_defaultWindowRect.left = 0;
        s_defaultWindowRect.top = 0;
        s_defaultWindowRect.right = width;
        s_defaultWindowRect.bottom = height;
    }

    *rect = s_defaultWindowRect;

    return 1;
}

// ʕ •ᴥ•ʔ✿ message is an SDL_EventType and lparam points at the SDL_Event ✿ ʕ •ᴥ•ʔ
intptr_t OsWindowProc(void* window, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    const SDL_Event& event = *reinterpret_cast<const SDL_Event*>(lparam);

    if (message >= SDL_EVENT_WINDOW_FIRST && message <= SDL_EVENT_WINDOW_LAST) {
        HandleWindowEvent(event.window);
        return 0;
    }

    switch (message) {
        case SDL_EVENT_KEYMAP_CHANGED:
            OsQueuePut(OS_INPUT_IME, 0, 0, 0, 0);
            break;

        case SDL_EVENT_TEXT_INPUT: {
            const char* text = event.text.text;
            if (text && s_typedChar && text[0] == static_cast<char>(s_typedChar) && text[1] == '\0') {
                s_typedChar = 0;
                break;
            }
            s_typedChar = 0;
            if (text) QueueText(text);
            break;
        }

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            SDL_Keycode code = event.key.key ? event.key.key : SDL_GetKeyFromScancode(event.key.scancode, SDL_KMOD_NONE, true);
            int32_t sym = (int32_t)code;
            switch (code) {
                case SDLK_RETURN: case SDLK_KP_ENTER: sym = 0xff0d; break;
                case SDLK_ESCAPE: sym = 0xff1b; break;
                case SDLK_BACKSPACE: sym = 0xff08; break;
                case SDLK_TAB: sym = 0xff09; break;
                case SDLK_DELETE: sym = 0xffff; break;
                case SDLK_LEFT: sym = 0xff51; break;
                case SDLK_UP: sym = 0xff52; break;
                case SDLK_RIGHT: sym = 0xff53; break;
                case SDLK_DOWN: sym = 0xff54; break;
                case SDLK_HOME: sym = 0xff50; break;
                case SDLK_END: sym = 0xff57; break;
                case SDLK_PAGEUP: sym = 0xff55; break;
                case SDLK_PAGEDOWN: sym = 0xff56; break;
                default: break;
            }
            if (WaylandEmbedTakeKey(sym, message == SDL_EVENT_KEY_DOWN)) break;
            KEY key;
            if (ConvertKeyCode(code, &key)) {
                OsQueuePut(message == SDL_EVENT_KEY_DOWN ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, event.key.repeat ? 1 : 0, 0, 0);
            }
            // ʕ •ᴥ•ʔ✿ Chat inserts from character events. SDL3 often marks text input active and then never sends the text ✿ ʕ •ᴥ•ʔ
            if (message == SDL_EVENT_KEY_DOWN && !(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
                SDL_Keycode typed = SDL_GetKeyFromScancode(event.key.scancode, event.key.mod, false);
                if (typed < 0x20 || typed >= 0x7F) typed = code;
                if (typed >= 0x20 && typed < 0x7F) {
                    s_typedChar = static_cast<int32_t>(typed);
                    OsQueuePut(OS_INPUT_CHAR, s_typedChar, event.key.repeat ? 1 : 0, 0, 0);
                }
            }
            break;
        }

        case SDL_EVENT_MOUSE_MOTION: {
            int32_t x = static_cast<int32_t>(event.motion.x);
            int32_t y = static_cast<int32_t>(event.motion.y);

            if (s_osMouseMode != OS_MOUSE_MODE_NORMAL) {
                x = static_cast<int32_t>(event.motion.xrel);
                y = static_cast<int32_t>(event.motion.yrel);
                ScalePoint(&x, &y, 1, 1);

                if (x || y) {
                    OsQueuePut(OS_INPUT_MOUSE_MOVE_RELATIVE, 0, x, y, 0);
                }
            } else {
                ScalePoint(&x, &y, 1);
                OsQueuePut(OS_INPUT_MOUSE_MOVE, 0, x, y, 0);
                SaveMouse(x, y);
            }
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            WaylandEmbedClearFocus();
            HandleMouseButton(event.button, OS_INPUT_MOUSE_DOWN);
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            HandleMouseButton(event.button, OS_INPUT_MOUSE_UP);
            break;

        case SDL_EVENT_MOUSE_WHEEL: {
            int32_t x;
            int32_t y;
            OsInputGetMousePosition(&x, &y);

            int32_t delta = event.wheel.integer_y;
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
            OsQueuePut(OS_INPUT_MOUSE_WHEEL, static_cast<int16_t>(delta * 120), x, y, 0);
            break;
        }

        default:
            break;
    }

    return 0;
}

void OsInputEnableSecureMode(int32_t enable) {
}
