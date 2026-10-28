#include <cmath>
#include <cstdint>
#include <cstring>
#include <SDL3/SDL.h>
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Tempest/C2Vector.h"

static const float s_axisMin = -32768.0f;
static const float s_axisMax = 32767.0f;

struct LnxJoystickStick {
    int32_t m_xAxis;
    int32_t m_yAxis;
};

struct LnxJoystickHat {
    bool m_enabled;
    int32_t m_buttons[4];
};

struct LnxJoystickMouseMode {
    int32_t m_enabled;
    int32_t m_stick;
    float m_speed;
    int32_t m_leftButton;
    int32_t m_rightButton;
    uint32_t m_lastTime;
    C2Vector m_accumulated;
};

struct LnxJoystickState {
    int32_t m_axes[8];
    uint8_t m_buttons[32];
    uint32_t m_pov[4];
};

struct LnxJoystick {
    int32_t m_deviceIndex;
    SDL_Joystick* m_device;
    SDL_Gamepad* m_pad;
    int32_t m_cursor;
    uint8_t m_held[24];
    uint32_t m_axisMask;
    LnxJoystickStick m_sticks[4];
    bool m_stickAxis[8];
    bool m_sliderAxis[8];
    LnxJoystickHat m_hats[4];
    bool m_hatButton[32];
    LnxJoystickMouseMode m_mouseMode;
    uint32_t m_stateIndex;
    LnxJoystickState m_states[2];
    int32_t m_axes[2][8];
};

static LnxJoystickMouseMode s_defaultMouseMode = { 1, 0, 1.5f, 0, 1, 0 };

static void ReleaseOwnedInput(LnxJoystick* joystick);

static int32_t s_initialized;
static uint32_t s_listId;

static TSGrowableArray<LnxJoystick> s_joysticks;

static void ResetState(LnxJoystickState* state) {
    memset(state, 0, sizeof(LnxJoystickState));

    for (int32_t i = 0; i < 4; i++) {
        state->m_pov[i] = 0xFFFFFFFF;
    }
}

static float NormalizeAxis(int32_t value) {
    return (static_cast<float>(value) - s_axisMin) / (s_axisMax - s_axisMin);
}

// OFFSET: 0x00870420
void OsJoystickMapStick(uint32_t index, int32_t stick, int32_t xAxis, int32_t yAxis) {
    if (index >= OsJoystickCount()) {
        return;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (stick >= 0 && stick < 4 && xAxis >= 0 && xAxis < 8 && (joystick->m_axisMask & (1 << xAxis)) && yAxis >= 0 && yAxis < 8 && (joystick->m_axisMask & (1 << yAxis))) {
        joystick->m_sticks[stick].m_xAxis = xAxis;
        joystick->m_sticks[stick].m_yAxis = yAxis;
        joystick->m_stickAxis[xAxis] = true;
        joystick->m_stickAxis[yAxis] = true;
    }
}

static bool AnyJoystickOpen() {
    for (uint32_t i = 0; i < s_joysticks.Count(); i++) {
        if (s_joysticks[i].m_device || s_joysticks[i].m_pad) {
            return true;
        }
    }

    return false;
}

static int32_t LiveDeviceCount() {
    int32_t padCount = 0;
    int32_t joyCount = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&padCount);
    SDL_JoystickID* joys = SDL_GetJoysticks(&joyCount);
    int32_t live = padCount;
    for (int32_t i = 0; joys && i < joyCount; i++) {
        int32_t seen = 0;
        for (int32_t p = 0; pads && p < padCount; p++) if (joys[i] == pads[p]) seen = 1;
        if (!seen) live++;
    }
    SDL_free(pads);
    SDL_free(joys);
    return live;
}

static bool ReplugAlreadyPickedUp() {
    if (LiveDeviceCount() != static_cast<int32_t>(s_joysticks.Count())) {
        return false;
    }

    for (uint32_t i = 0; i < s_joysticks.Count(); i++) {
        const LnxJoystick* joystick = &s_joysticks[i];

        if (joystick->m_device || joystick->m_pad || joystick->m_axisMask) {
            return false;
        }

        for (int32_t stick = 0; stick < 4; stick++) {
            if (joystick->m_sticks[stick].m_xAxis != -1 || joystick->m_sticks[stick].m_yAxis != -1) {
                return false;
            }
        }

        for (int32_t state = 0; state < 2; state++) {
            for (int32_t pov = 0; pov < 4; pov++) {
                if (joystick->m_states[state].m_pov[pov] != 0) {
                    return false;
                }
            }
        }
    }

    return true;
}

static void DropJoystickListIfIdle() {
    if (!s_initialized || AnyJoystickOpen() || ReplugAlreadyPickedUp()) {
        return;
    }

    s_joysticks.SetCount(0);
    s_initialized = 0;
}

uint32_t OsJoystickListId() {
    return s_listId;
}

uint32_t OsJoystickCount() {
    DropJoystickListIfIdle();

    if (!s_initialized) {
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, "1");

        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            return 0;
        }

        s_initialized = 1;
        s_listId++;

        SDL_SetJoystickEventsEnabled(false);

        int32_t padCount = 0;
        SDL_JoystickID* pads = SDL_GetGamepads(&padCount);
        int32_t joyCount = 0;
        SDL_JoystickID* joys = SDL_GetJoysticks(&joyCount);

        auto add = [&](SDL_JoystickID id) {
            LnxJoystick* joystick = s_joysticks.New();
            memset(joystick, 0, sizeof(LnxJoystick));
            joystick->m_deviceIndex = static_cast<int32_t>(id);
            joystick->m_mouseMode = s_defaultMouseMode;
            for (int32_t stick = 0; stick < 4; stick++) {
                joystick->m_sticks[stick].m_xAxis = -1;
                joystick->m_sticks[stick].m_yAxis = -1;
            }
        };

        for (int32_t i = 0; pads && i < padCount; i++) add(pads[i]);
        for (int32_t i = 0; joys && i < joyCount; i++) {
            int32_t seen = 0;
            for (int32_t p = 0; pads && p < padCount; p++) if (joys[i] == pads[p]) seen = 1;
            if (!seen) add(joys[i]);
        }
        SDL_free(pads);
        SDL_free(joys);
    }

    return s_joysticks.Count();
}

uint32_t OsJoystickOpen(uint32_t index) {
    if (index >= OsJoystickCount()) {
        return 0xFFFFFFFF;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device && !joystick->m_pad) {
        SDL_JoystickID id = static_cast<SDL_JoystickID>(joystick->m_deviceIndex);
        if (SDL_IsGamepad(id)) {
            joystick->m_pad = SDL_OpenGamepad(id);
            joystick->m_mouseMode.m_enabled = 0;
        } else {
            joystick->m_device = SDL_OpenJoystick(id);
        }
        if (!joystick->m_device && !joystick->m_pad) {
            return 0xFFFFFFFF;
        }
    }

    int32_t axes = joystick->m_pad ? 6 : SDL_GetNumJoystickAxes(joystick->m_device);
    if (axes < 0) {
        axes = 0;
    }
    joystick->m_axisMask = (1u << (axes > 8 ? 8 : axes)) - 1;

    ResetState(&joystick->m_states[0]);
    ResetState(&joystick->m_states[1]);
    OsJoystickMapStick(index, 0, 0, 1);

    return index;
}

// OFFSET: 0x00870840
void OsJoystickClose(uint32_t index) {
    if (index >= OsJoystickCount()) {
        return;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (joystick->m_pad) {
        SDL_CloseGamepad(joystick->m_pad);
        joystick->m_pad = nullptr;
    }
    if (joystick->m_device) {
        SDL_CloseJoystick(joystick->m_device);
        joystick->m_device = nullptr;
    }

    if (index == 0) {
        ReleaseOwnedInput(joystick);
    }
}

char* OsJoystickGetName(uint32_t index, char* buffer, uint32_t size) {
    if (index >= OsJoystickCount()) {
        return nullptr;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device && !joystick->m_pad) {
        return nullptr;
    }

    const char* name = joystick->m_pad ? SDL_GetGamepadName(joystick->m_pad) : SDL_GetJoystickName(joystick->m_device);
    SStrCopy(buffer, name ? name : "", size);

    return buffer;
}

// OFFSET: 0x008708D0
void OsJoystickMapSlider(uint32_t index, int32_t axis) {
    if (index >= OsJoystickCount()) {
        return;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (joystick->m_device && axis >= 0 && axis < 8) {
        joystick->m_sliderAxis[axis] = true;
    }
}

// OFFSET: 0x00870910
void OsJoystickMapHat(uint32_t index, int32_t hat, int32_t up, int32_t right, int32_t down, int32_t left) {
    if (index >= OsJoystickCount()) {
        return;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (joystick->m_device && hat >= 0 && hat < 4 && up >= 0 && up < 32 && right >= 0 && right < 32 && down >= 0 && down < 32 && left >= 0 && left < 32) {
        joystick->m_hats[hat].m_buttons[0] = up;
        joystick->m_hats[hat].m_buttons[1] = right;
        joystick->m_hats[hat].m_buttons[2] = down;
        joystick->m_hats[hat].m_buttons[3] = left;
        joystick->m_hats[hat].m_enabled = true;
        joystick->m_hatButton[up] = true;
        joystick->m_hatButton[right] = true;
        joystick->m_hatButton[down] = true;
        joystick->m_hatButton[left] = true;
    }
}

// OFFSET: 0x008709E0
void OsJoystickSetMouseMode(uint32_t index, int32_t enable) {
    if (index >= OsJoystickCount()) {
        return;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device) {
        return;
    }

    LnxJoystickMouseMode* mouseMode = &joystick->m_mouseMode;

    if (enable == mouseMode->m_enabled) {
        return;
    }

    mouseMode->m_enabled = enable;
    mouseMode->m_lastTime = 0;
    mouseMode->m_accumulated.x = 0.0f;
    mouseMode->m_accumulated.y = 0.0f;

    if (!enable) {
        return;
    }

    LnxJoystickState* state = &joystick->m_states[joystick->m_stateIndex];
    int32_t* axes = joystick->m_axes[joystick->m_stateIndex];
    LnxJoystickStick* stick = &joystick->m_sticks[mouseMode->m_stick];

    if (stick->m_xAxis >= 0 && stick->m_yAxis >= 0 && (axes[stick->m_xAxis] || axes[stick->m_yAxis])) {
        OsQueuePut(OS_INPUT_JOYSTICK_STICK, mouseMode->m_stick, 0, 0, 0);
    }

    int32_t x;
    int32_t y;

    if (state->m_buttons[mouseMode->m_leftButton]) {
        OsInputGetMousePosition(&x, &y);
        OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, x, y, 0);
    }

    if (state->m_buttons[mouseMode->m_rightButton]) {
        OsInputGetMousePosition(&x, &y);
        OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_RIGHT, x, y, 0);
    }
}

// OFFSET: 0x00870B00
void OsJoystickConfigureMouseMode(uint32_t index, int32_t stick, int32_t leftButton, int32_t rightButton, float speed) {
    if (index >= OsJoystickCount()) {
        return;
    }

    LnxJoystick* joystick = &s_joysticks[index];

    if (joystick->m_device && stick >= 0 && stick < 4 && joystick->m_sticks[stick].m_xAxis >= 0 && joystick->m_sticks[stick].m_yAxis >= 0) {
        joystick->m_mouseMode.m_speed = speed;
        joystick->m_mouseMode.m_stick = stick;
        joystick->m_mouseMode.m_leftButton = leftButton;
        joystick->m_mouseMode.m_rightButton = rightButton;
    }
}

static uint32_t HatToPov(uint32_t directions) {
    switch (directions) {
        case 1:
            return 0;

        case 2:
            return 9000;

        case 3:
            return 4500;

        case 4:
            return 18000;

        case 6:
            return 13500;

        case 8:
            return 27000;

        case 9:
            return 31500;

        case 12:
            return 22500;

        default:
            return 0xFFFFFFFF;
    }
}

static void ReadState(LnxJoystick* joystick, LnxJoystickState* state) {
    if (joystick->m_pad) {
        static const SDL_GamepadAxis kAxes[] = {
            SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
            SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
        };
        for (int32_t axis = 0; axis < 8; axis++) {
            state->m_axes[axis] = axis < 6 ? SDL_GetGamepadAxis(joystick->m_pad, kAxes[axis]) : 0;
        }
        for (int32_t button = 0; button < 32; button++) {
            state->m_buttons[button] = button < SDL_GAMEPAD_BUTTON_COUNT && SDL_GetGamepadButton(joystick->m_pad, static_cast<SDL_GamepadButton>(button));
        }
        for (int32_t hat = 0; hat < 4; hat++) state->m_pov[hat] = 0xFFFFFFFF;
        return;
    }

    SDL_Joystick* device = joystick->m_device;

    for (int32_t axis = 0; axis < 8; axis++) {
        state->m_axes[axis] = joystick->m_axisMask & (1 << axis) ? SDL_GetJoystickAxis(device, axis) : 0;
    }

    int32_t buttons = SDL_GetNumJoystickButtons(device);

    for (int32_t button = 0; button < 32; button++) {
        state->m_buttons[button] = button < buttons ? SDL_GetJoystickButton(device, button) : 0;
    }

    int32_t hats = SDL_GetNumJoystickHats(device);

    for (int32_t hat = 0; hat < 4; hat++) {
        state->m_pov[hat] = hat < hats ? HatToPov(SDL_GetJoystickHat(device, hat)) : 0xFFFFFFFF;
    }
}

static float g_lookX;
static float g_lookY;
static int32_t g_cursor;
static char g_padName[128];
static OsGamepadOptions g_options = { 0, 0.18f, 1.0f, 16.0f, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 };

void OsGamepadSetOptions(const OsGamepadOptions* options) {
    if (!options) {
        return;
    }

    g_options = *options;
    if (g_options.deadzone < 0.0f) {
        g_options.deadzone = 0.0f;
    }
    if (g_options.deadzone > 0.9f) {
        g_options.deadzone = 0.9f;
    }
    if (g_options.lookSens < 0.05f) {
        g_options.lookSens = 0.05f;
    }
    if (g_options.cursorSpeed < 1.0f) {
        g_options.cursorSpeed = 1.0f;
    }
}

const char* OsGamepadConnectedName() {
    int32_t count = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&count);
    const char* name = pads && count > 0 ? SDL_GetGamepadNameForID(pads[0]) : nullptr;
    SDL_free(pads);

    if (!name || !name[0]) {
        int32_t joyCount = 0;
        SDL_JoystickID* joys = SDL_GetJoysticks(&joyCount);
        name = joys && joyCount > 0 ? SDL_GetJoystickNameForID(joys[0]) : nullptr;
        SDL_free(joys);
    }

    SStrCopy(g_padName, name ? name : "", sizeof(g_padName));
    return g_padName;
}

static float Stick(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
    float value = SDL_GetGamepadAxis(pad, axis) / 32767.0f;
    float deadzone = g_options.deadzone;
    return value < deadzone && value > -deadzone ? 0.0f : value;
}

static void HoldKey(LnxJoystick* joystick, int slot, KEY key, int down) {
    if (down == joystick->m_held[slot]) return;
    joystick->m_held[slot] = static_cast<uint8_t>(down);
    OsQueuePut(down ? OS_INPUT_KEY_DOWN : OS_INPUT_KEY_UP, key, 1, 0, 0);
}

static void EdgeKey(LnxJoystick* joystick, int slot, int down, KEY key) {
    if (down == joystick->m_held[slot]) return;
    joystick->m_held[slot] = static_cast<uint8_t>(down);
    if (!down) return;
    OsQueuePut(OS_INPUT_KEY_DOWN, key, 1, 0, 0);
    OsQueuePut(OS_INPUT_KEY_UP, key, 1, 0, 0);
}

static void ReleaseMove(LnxJoystick* joystick) {
    HoldKey(joystick, 0, KEY_W, 0);
    HoldKey(joystick, 1, KEY_S, 0);
    HoldKey(joystick, 2, KEY_Q, 0);
    HoldKey(joystick, 22, KEY_A, 0);
    HoldKey(joystick, 3, KEY_E, 0);
    HoldKey(joystick, 23, KEY_D, 0);
    HoldKey(joystick, 5, KEY_SPACE, 0);
    HoldKey(joystick, 6, KEY_ESCAPE, 0);
    HoldKey(joystick, 19, KEY_Z, 0);
    HoldKey(joystick, 20, KEY_2, 0);
    HoldKey(joystick, 21, KEY_1, 0);
    if (joystick->m_held[9]) {
        int32_t x = 0;
        int32_t y = 0;
        OsInputGetMousePosition(&x, &y);
        OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, x, y, 0);
        joystick->m_held[9] = 0;
    }
}

static void ReleasePresetButtons(LnxJoystick* joystick) {
    HoldKey(joystick, 5, KEY_SPACE, 0);
    HoldKey(joystick, 6, KEY_ESCAPE, 0);
    HoldKey(joystick, 19, KEY_Z, 0);
    HoldKey(joystick, 20, KEY_2, 0);
    HoldKey(joystick, 21, KEY_1, 0);
    if (joystick->m_held[9]) {
        int32_t x = 0;
        int32_t y = 0;
        OsInputGetMousePosition(&x, &y);
        OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, x, y, 0);
        joystick->m_held[9] = 0;
    }
}

// ʕ •ᴥ•ʔ✿ SDL maps a GameCube Z to the right shoulder and L/R to the analog triggers ✿ ʕ •ᴥ•ʔ
static const char* kButtonKeys[16] = {
    "JOYBUTTON1", "JOYBUTTON2", "JOYBUTTON3", "JOYBUTTON4",
    "JOYBUTTON5", "JOYBUTTON6", "JOYBUTTON7", "JOYBUTTON8",
    "JOYBUTTON9", "JOYBUTTON10", "JOYBUTTON11", "JOYBUTTON12",
    "JOYBUTTON13", "JOYBUTTON14", "JOYBUTTON15", "JOYBUTTON16",
};

static const char* kAxisKeys[8] = {
    "JOYAXIS1NEG", "JOYAXIS1POS", "JOYAXIS2NEG", "JOYAXIS2POS",
    "JOYAXIS3NEG", "JOYAXIS3POS", "JOYAXIS4NEG", "JOYAXIS4POS",
};

static const char* kStandardLabels[16] = {
    "A", "B", "X", "Y", "LB", "RB", "Back", "Start",
    "LS", "RS", "D-Up", "D-Down", "D-Left", "D-Right", "LT", "RT",
};

static const char* kCubeLabels[16] = {
    "A", "B", "X", "Y", "LB", "Z", "Back", "Start",
    "LS", "RS", "D-Up", "D-Down", "D-Left", "D-Right", "L", "R",
};

static uint8_t g_slotHeld[16];
static uint8_t g_axisHeld[8];
static uint8_t g_buttonDown[16];
static int32_t g_capture;
static OsGamepadEdge g_edges[32];
static int32_t g_edgeRead;
static int32_t g_edgeWrite;
static char g_captureKeys[8][24];
static int32_t g_captureRead;
static int32_t g_captureWrite;

static int NameIsCube(const char* name) {
    return name && (SStrStrI(name, "gamecube") || SStrStrI(name, "game cube") || SStrStrI(name, "wup-028"));
}

static void PushCapture(const char* key) {
    int32_t next = (g_captureWrite + 1) % 8;
    if (next == g_captureRead) {
        g_captureRead = (g_captureRead + 1) % 8;
    }
    SStrCopy(g_captureKeys[g_captureWrite], key, sizeof(g_captureKeys[g_captureWrite]));
    g_captureWrite = next;
}

static void PushEdge(const char* key, int down) {
    int32_t next = (g_edgeWrite + 1) % 32;
    if (next == g_edgeRead) {
        g_edgeRead = (g_edgeRead + 1) % 32;
    }
    SStrCopy(g_edges[g_edgeWrite].key, key, sizeof(g_edges[g_edgeWrite].key));
    g_edges[g_edgeWrite].down = down;
    g_edgeWrite = next;
    if (down) {
        PushCapture(key);
    }
}

static void EdgeHeld(uint8_t* held, int slot, int down, const char* key) {
    down = down ? 1 : 0;
    if (held[slot] == down) {
        return;
    }
    held[slot] = static_cast<uint8_t>(down);
    PushEdge(key, down);
}

static void ReleaseEdges() {
    for (int32_t i = 0; i < 16; i++) {
        g_buttonDown[i] = 0;
        if (g_slotHeld[i]) {
            g_slotHeld[i] = 0;
            PushEdge(kButtonKeys[i], 0);
        }
    }
    for (int32_t i = 0; i < 8; i++) {
        if (g_axisHeld[i]) {
            g_axisHeld[i] = 0;
            PushEdge(kAxisKeys[i], 0);
        }
    }
}

static void ReleaseOwnedInput(LnxJoystick* joystick) {
    ReleaseMove(joystick);
    ReleaseEdges();
    g_lookX = 0.0f;
    g_lookY = 0.0f;
    g_cursor = 0;
    if (joystick) {
        joystick->m_cursor = 0;
    }
}

static int SlotDown(SDL_Gamepad* pad, int slot) {
    static const SDL_GamepadButton kButtons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
        SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    };

    if (!pad) {
        return 0;
    }
    if (slot < 14) {
        return SDL_GetGamepadButton(pad, kButtons[slot]);
    }
    if (slot == 14) {
        return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
    }
    if (slot == 15) {
        return SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    }
    return 0;
}

static void EmitAxis(int slot, float value) {
    float dead = g_options.deadzone;
    EdgeHeld(g_axisHeld, slot * 2, value < -dead, kAxisKeys[slot * 2]);
    EdgeHeld(g_axisHeld, slot * 2 + 1, value > dead, kAxisKeys[slot * 2 + 1]);
}

static void EmitAxes(float lx, float ly, float rx, float ry, int enabled) {
    if (!enabled) {
        for (int32_t i = 0; i < 8; i++) {
            EdgeHeld(g_axisHeld, i, 0, kAxisKeys[i]);
        }
        return;
    }
    EmitAxis(0, lx);
    EmitAxis(1, ly);
    EmitAxis(2, rx);
    EmitAxis(3, ry);
}

static int PovDown(uint32_t pov, int slot) {
    int up = pov == 0 || pov == 4500 || pov == 31500;
    int right = pov == 9000 || pov == 4500 || pov == 13500;
    int down = pov == 18000 || pov == 13500 || pov == 22500;
    int left = pov == 27000 || pov == 22500 || pov == 31500;
    if (slot == 10) return up;
    if (slot == 11) return down;
    if (slot == 12) return left;
    if (slot == 13) return right;
    return 0;
}

static void EmitPad(LnxJoystick* joystick, int active) {
    int axisBind = g_options.layout == 2 && g_options.axisBindings;
    float lx = 0.0f;
    float ly = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;

    if (active && joystick->m_pad) {
        lx = Stick(joystick->m_pad, SDL_GAMEPAD_AXIS_LEFTX);
        ly = Stick(joystick->m_pad, SDL_GAMEPAD_AXIS_LEFTY);
        rx = Stick(joystick->m_pad, SDL_GAMEPAD_AXIS_RIGHTX);
        ry = Stick(joystick->m_pad, SDL_GAMEPAD_AXIS_RIGHTY);
    }

    for (int32_t i = 0; i < 16; i++) {
        int down = active ? SlotDown(joystick->m_pad, i) : 0;
        g_buttonDown[i] = static_cast<uint8_t>(down);
        EdgeHeld(g_slotHeld, i, down, kButtonKeys[i]);
    }
    EmitAxes(lx, ly, rx, ry, axisBind);
}

static void EmitRaw(LnxJoystick* joystick, const LnxJoystickState* state) {
    int axisBind = g_options.layout == 2 && g_options.axisBindings;
    float lx = (joystick->m_axisMask & 1) ? state->m_axes[0] / 32767.0f : 0.0f;
    float ly = (joystick->m_axisMask & 2) ? state->m_axes[1] / 32767.0f : 0.0f;
    float rx = (joystick->m_axisMask & 4) ? state->m_axes[2] / 32767.0f : 0.0f;
    float ry = (joystick->m_axisMask & 8) ? state->m_axes[3] / 32767.0f : 0.0f;
    float dead = g_options.deadzone;
    if (lx < dead && lx > -dead) lx = 0.0f;
    if (ly < dead && ly > -dead) ly = 0.0f;
    if (rx < dead && rx > -dead) rx = 0.0f;
    if (ry < dead && ry > -dead) ry = 0.0f;

    for (int32_t i = 0; i < 16; i++) {
        int down = state->m_buttons[i];
        if (i >= 10 && i <= 13) {
            down = down || PovDown(state->m_pov[0], i);
        }
        g_buttonDown[i] = static_cast<uint8_t>(down ? 1 : 0);
        EdgeHeld(g_slotHeld, i, down, kButtonKeys[i]);
    }
    EmitAxes(lx, ly, rx, ry, axisBind);
}

static int DeviceMapped() {
    if (!s_joysticks.Count()) {
        return 0;
    }
    const LnxJoystick* joystick = &s_joysticks[0];
    if (joystick->m_pad) {
        return 1;
    }
    return SDL_IsGamepad(static_cast<SDL_JoystickID>(joystick->m_deviceIndex));
}

static int PositionalLabels() {
    if (DeviceMapped()) {
        return 1;
    }
    return s_joysticks.Count() == 0;
}

int32_t OsGamepadKind() {
    if (g_options.style == 2) {
        return OS_GAMEPAD_GAMECUBE;
    }
    if (g_options.style == 1) {
        return OS_GAMEPAD_STANDARD;
    }
    if (!s_joysticks.Count()) {
        const char* name = OsGamepadConnectedName();
        if (NameIsCube(name)) {
            return OS_GAMEPAD_GAMECUBE;
        }
        return name && name[0] ? OS_GAMEPAD_STANDARD : OS_GAMEPAD_NONE;
    }

    const LnxJoystick* joystick = &s_joysticks[0];
    const char* name = nullptr;
    if (joystick->m_pad) {
        if (SDL_GetGamepadType(joystick->m_pad) == SDL_GAMEPAD_TYPE_GAMECUBE) {
            return OS_GAMEPAD_GAMECUBE;
        }
        name = SDL_GetGamepadName(joystick->m_pad);
    } else if (joystick->m_device) {
        name = SDL_GetJoystickName(joystick->m_device);
    }
    if (NameIsCube(name)) {
        return OS_GAMEPAD_GAMECUBE;
    }
    return OS_GAMEPAD_STANDARD;
}

int32_t OsGamepadMapped() {
    return DeviceMapped();
}

int32_t OsGamepadControlCount() {
    return 16;
}

int32_t OsGamepadControl(int32_t index, char* key, uint32_t keySize, char* label, uint32_t labelSize, int32_t* down) {
    if (index < 0 || index >= 16 || !key || !label) {
        return 0;
    }

    SStrCopy(key, kButtonKeys[index], keySize);
    const char* text = kStandardLabels[index];
    if (!PositionalLabels()) {
        static char raw[16];
        SStrPrintf(raw, sizeof(raw), "Button %d", index + 1);
        text = raw;
    } else if (OsGamepadKind() == OS_GAMEPAD_GAMECUBE) {
        text = kCubeLabels[index];
    }
    SStrCopy(label, text, labelSize);
    if (down) {
        *down = g_buttonDown[index];
    }
    return 1;
}

int32_t OsGamepadDrainEdges(OsGamepadEdge* out, int32_t max) {
    int32_t count = 0;
    while (out && count < max && g_edgeRead != g_edgeWrite) {
        out[count] = g_edges[g_edgeRead];
        g_edgeRead = (g_edgeRead + 1) % 32;
        count++;
    }
    return count;
}

const char* OsGamepadPopCapture() {
    if (g_captureRead == g_captureWrite) {
        return "";
    }
    const char* key = g_captureKeys[g_captureRead];
    g_captureRead = (g_captureRead + 1) % 8;
    return key;
}

void OsGamepadSetCapture(int32_t on) {
    on = on ? 1 : 0;
    if (g_capture == on) {
        return;
    }
    g_capture = on;
    if (on && s_joysticks.Count()) {
        ReleaseOwnedInput(&s_joysticks[0]);
    }
}

int32_t OsGamepadCapture() {
    return g_capture;
}

static void ApplyAxes(LnxJoystick* joystick, float lx, float ly, float rx, float ry) {
    int axisBind = g_options.layout == 2 && g_options.axisBindings;
    if (axisBind || g_capture) {
        HoldKey(joystick, 0, KEY_W, 0);
        HoldKey(joystick, 1, KEY_S, 0);
        HoldKey(joystick, 2, KEY_Q, 0);
        HoldKey(joystick, 22, KEY_A, 0);
        HoldKey(joystick, 3, KEY_E, 0);
        HoldKey(joystick, 23, KEY_D, 0);
        g_lookX = 0.0f;
        g_lookY = 0.0f;
        return;
    }

    HoldKey(joystick, 0, KEY_W, ly < 0);
    HoldKey(joystick, 1, KEY_S, ly > 0);
    int32_t strafe = g_options.strafe;
    HoldKey(joystick, 2, KEY_Q, strafe && lx < 0);
    HoldKey(joystick, 22, KEY_A, !strafe && lx < 0);
    HoldKey(joystick, 3, KEY_E, strafe && lx > 0);
    HoldKey(joystick, 23, KEY_D, !strafe && lx > 0);

    int stick = g_options.cursorToggle && joystick->m_pad && g_buttonDown[9];
    if (stick && !joystick->m_held[4]) {
        joystick->m_cursor = !joystick->m_cursor;
    }
    joystick->m_held[4] = static_cast<uint8_t>(stick);
    g_cursor = joystick->m_cursor;

    if (joystick->m_cursor) {
        g_lookX = 0.0f;
        g_lookY = 0.0f;
        if (rx || ry) {
            int32_t x = 0;
            int32_t y = 0;
            OsInputGetMousePosition(&x, &y);
            OsInputSetMousePosition(x + static_cast<int32_t>(rx * g_options.cursorSpeed), y + static_cast<int32_t>(ry * g_options.cursorSpeed));
        }
    } else {
        float lookY = g_options.invertY ? -ry : ry;
        g_lookX = rx * g_options.lookSens;
        g_lookY = lookY * g_options.lookSens;
    }
}

// ʕ •ᴥ•ʔ✿ Retail layout, or nothing when Steam Input is already sending keys ✿ ʕ •ᴥ•ʔ
static void ApplyRetail(LnxJoystick* joystick) {
    SDL_Gamepad* pad = joystick->m_pad;
    if (g_options.layout == 1) {
        ReleaseMove(joystick);
        joystick->m_cursor = 0;
        g_cursor = 0;
        g_lookX = 0.0f;
        g_lookY = 0.0f;
        return;
    }

    float lx = Stick(pad, SDL_GAMEPAD_AXIS_LEFTX);
    float ly = Stick(pad, SDL_GAMEPAD_AXIS_LEFTY);
    float rx = Stick(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    float ry = Stick(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    ApplyAxes(joystick, lx, ly, rx, ry);

    int preset = g_options.layout == 0 && !g_capture;
    if (!preset) {
        ReleasePresetButtons(joystick);
        return;
    }

    int south = g_options.south && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH);
    if (joystick->m_cursor) {
        HoldKey(joystick, 5, KEY_SPACE, 0);
        if (south != joystick->m_held[9]) {
            int32_t x = 0;
            int32_t y = 0;
            OsInputGetMousePosition(&x, &y);
            OsQueuePut(south ? OS_INPUT_MOUSE_DOWN : OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, x, y, 0);
        }
        joystick->m_held[9] = static_cast<uint8_t>(south);
    } else {
        if (joystick->m_held[9]) {
            int32_t x = 0;
            int32_t y = 0;
            OsInputGetMousePosition(&x, &y);
            OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_LEFT, x, y, 0);
            joystick->m_held[9] = 0;
        }
        HoldKey(joystick, 5, KEY_SPACE, south);
    }

    HoldKey(joystick, 6, KEY_ESCAPE, g_options.east && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST));
    if (g_options.west && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST) && !joystick->m_held[18]) {
        int32_t x = 0;
        int32_t y = 0;
        OsInputGetMousePosition(&x, &y);
        OsQueuePut(OS_INPUT_MOUSE_DOWN, MOUSE_BUTTON_RIGHT, x, y, 0);
        OsQueuePut(OS_INPUT_MOUSE_UP, MOUSE_BUTTON_RIGHT, x, y, 0);
    }
    joystick->m_held[18] = static_cast<uint8_t>(g_options.west && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST));
    HoldKey(joystick, 19, KEY_Z, g_options.north && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH));

    EdgeKey(joystick, 7, g_options.shoulders && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), KEY_TAB);
    if (g_options.shoulders && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && !joystick->m_held[8]) {
        OsQueuePut(OS_INPUT_KEY_DOWN, KEY_LSHIFT, 1, 0, 0);
        OsQueuePut(OS_INPUT_KEY_DOWN, KEY_TAB, 1, 0, 0);
        OsQueuePut(OS_INPUT_KEY_UP, KEY_TAB, 1, 0, 0);
        OsQueuePut(OS_INPUT_KEY_UP, KEY_LSHIFT, 1, 0, 0);
    }
    joystick->m_held[8] = static_cast<uint8_t>(g_options.shoulders && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));

    int rightTrigger = g_options.triggers && SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    int leftTrigger = g_options.triggers && SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
    HoldKey(joystick, 21, KEY_1, rightTrigger);
    HoldKey(joystick, 20, KEY_2, leftTrigger);

    EdgeKey(joystick, 11, g_options.back && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK), KEY_M);
    EdgeKey(joystick, 12, g_options.start && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START), KEY_ESCAPE);
    EdgeKey(joystick, 13, g_options.autorun && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK), KEY_NUMLOCK);
    EdgeKey(joystick, 14, g_options.dpad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP), KEY_C);
    EdgeKey(joystick, 15, g_options.dpad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN), KEY_B);
    EdgeKey(joystick, 16, g_options.dpad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT), KEY_P);
    EdgeKey(joystick, 17, g_options.dpad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT), KEY_L);
}

static int DriveAsPad(const LnxJoystick* joystick) {
    if (joystick->m_pad) {
        return 1;
    }
    if (g_options.style != 0) {
        return 1;
    }
    const char* name = joystick->m_device ? SDL_GetJoystickName(joystick->m_device) : nullptr;
    return NameIsCube(name);
}

// OFFSET: 0x00870B80
static void PollJoystick(uint32_t index, int32_t active) {
    LnxJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device && !joystick->m_pad) {
        return;
    }

    if (joystick->m_pad ? !SDL_GamepadConnected(joystick->m_pad) : !SDL_JoystickConnected(joystick->m_device)) {
        OsJoystickClose(index);
        return;
    }

    uint32_t previous = joystick->m_stateIndex;
    uint32_t current = previous == 0;
    joystick->m_stateIndex = current;

    LnxJoystickState* previousState = &joystick->m_states[previous];
    LnxJoystickState* state = &joystick->m_states[current];
    int32_t* previousAxes = joystick->m_axes[previous];
    int32_t* axes = joystick->m_axes[current];

    if (active && (joystick->m_pad || joystick->m_device)) {
        ReadState(joystick, state);
    } else {
        ResetState(state);
    }

    // ʕ •ᴥ•ʔ✿ Steam Input already emits the keys. Don't also turn the pad into buttons. ✿ ʕ •ᴥ•ʔ
    if (joystick->m_pad && g_options.layout == 1) {
        if (index == 0) {
            ReleaseEdges();
        }
        if (active) {
            ApplyRetail(joystick);
        }
        return;
    }

    for (int32_t i = 0; i < 4; i++) {
        int32_t xAxis = joystick->m_sticks[i].m_xAxis;
        int32_t yAxis = joystick->m_sticks[i].m_yAxis;

        if (xAxis >= 0 && yAxis >= 0) {
            float x = (NormalizeAxis(state->m_axes[xAxis]) - 0.5f) * 2.0f;
            float y = (NormalizeAxis(state->m_axes[yAxis]) - 0.5f) * 2.0f;
            float length = sqrtf(y * y + x * x);

            if (length <= 0.15f) {
                axes[xAxis] = 0;
                axes[yAxis] = 0;
            } else {
                float scale = (length - 0.15f) / (length - length * 0.15f);
                axes[xAxis] = static_cast<int32_t>(x * scale * 32767.0f);
                axes[yAxis] = static_cast<int32_t>(scale * y * 32767.0f);
            }
        }
    }

    for (int32_t axis = 0; axis < 8; axis++) {
        if ((joystick->m_axisMask & (1 << axis)) && !joystick->m_stickAxis[axis]) {
            float value = NormalizeAxis(state->m_axes[axis]);

            if (!joystick->m_sliderAxis[axis]) {
                value = (value - 0.5f) * 2.0f;
            }

            if (value > 0.15f) {
                axes[axis] = static_cast<int32_t>((value - 0.15f) * 1.1764705f * 32767.0f);
            } else if (value >= -0.15f) {
                axes[axis] = 0;
            } else {
                axes[axis] = static_cast<int32_t>((value + 0.15f) * 1.1764705f * 32767.0f);
            }
        }
    }

    for (int32_t i = 0; i < 4; i++) {
        LnxJoystickHat* hat = &joystick->m_hats[i];

        if (hat->m_enabled) {
            uint32_t directions = state->m_buttons[hat->m_buttons[0]] != 0;

            if (state->m_buttons[hat->m_buttons[1]]) {
                directions |= 2;
            }

            if (state->m_buttons[hat->m_buttons[2]]) {
                directions |= 4;
            }

            if (state->m_buttons[hat->m_buttons[3]]) {
                directions |= 8;
            }

            state->m_pov[i] = HatToPov(directions);
        }
    }

    LnxJoystickMouseMode* mouseMode = &joystick->m_mouseMode;

    if (!joystick->m_pad && DriveAsPad(joystick)) {
        mouseMode->m_enabled = 0;
    }

    if (!joystick->m_pad && mouseMode->m_enabled && joystick->m_sticks[0].m_xAxis >= 0) {
        uint32_t now = OsGetAsyncTimeMs();

        if (mouseMode->m_lastTime) {
            float x = axes[joystick->m_sticks[0].m_xAxis] * (1.0f / 32767.0f);
            float y = (1.0f / 32767.0f) * axes[joystick->m_sticks[0].m_yAxis];
            float y2 = y * y;
            float x2 = x * x;

            if (y2 + x2 > 0.0f) {
                tagRECT rect;
                OsGetDefaultWindowRect(&rect);

                float distance = static_cast<int32_t>(now - mouseMode->m_lastTime) * 0.001f * (rect.right - rect.left) * mouseMode->m_speed;

                float dx = x * (x2 * x) * distance;
                mouseMode->m_accumulated.x = x < 0.0f ? mouseMode->m_accumulated.x - dx : dx + mouseMode->m_accumulated.x;

                float dy = distance * (y * (y2 * y));
                mouseMode->m_accumulated.y = y < 0.0f ? mouseMode->m_accumulated.y - dy : dy + mouseMode->m_accumulated.y;

                int32_t moveX = static_cast<int32_t>(mouseMode->m_accumulated.x);
                int32_t moveY = static_cast<int32_t>(mouseMode->m_accumulated.y);

                if (moveX || moveY) {
                    mouseMode->m_accumulated.x -= moveX;
                    mouseMode->m_accumulated.y -= moveY;

                    int32_t mouseX;
                    int32_t mouseY;
                    OsInputGetMousePosition(&mouseX, &mouseY);
                    OsInputSetMousePosition(moveX + mouseX, moveY + mouseY);
                }
            }
        }

        mouseMode->m_lastTime = now;
    }

    for (int32_t i = 0; i < 4; i++) {
        int32_t xAxis = joystick->m_sticks[i].m_xAxis;
        int32_t yAxis = joystick->m_sticks[i].m_yAxis;

        if (xAxis >= 0 && yAxis >= 0 && (!mouseMode->m_enabled || i != mouseMode->m_stick)) {
            if (previousAxes[xAxis] != axes[xAxis] || previousAxes[yAxis] != axes[yAxis]) {
                OsQueuePut(OS_INPUT_JOYSTICK_STICK, i, axes[xAxis], axes[yAxis], 0);
            }
        }
    }

    LnxJoystickStick* mouseStick = &joystick->m_sticks[mouseMode->m_stick];

    for (int32_t axis = 0; axis < 8; axis++) {
        if ((joystick->m_axisMask & (1 << axis)) && !joystick->m_stickAxis[axis]) {
            if (!mouseMode->m_enabled || (axis != mouseStick->m_xAxis && axis != mouseStick->m_yAxis)) {
                if (previousAxes[axis] != axes[axis]) {
                    OsQueuePut(OS_INPUT_JOYSTICK_AXIS, axis, axes[axis], 0, 0);
                }
            }
        }
    }

    for (int32_t button = 0; button < 32; button++) {
        if (joystick->m_hatButton[button]) {
            continue;
        }

        uint8_t down = state->m_buttons[button];

        if (previousState->m_buttons[button] == down) {
            continue;
        }

        if (mouseMode->m_enabled && (button == mouseMode->m_leftButton || button == mouseMode->m_rightButton)) {
            int32_t x;
            int32_t y;
            OsInputGetMousePosition(&x, &y);

            MOUSEBUTTON mouseButton = button != mouseMode->m_leftButton ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT;
            OsQueuePut(down ? OS_INPUT_MOUSE_DOWN : OS_INPUT_MOUSE_UP, mouseButton, x, y, 0);
        } else {
            OsQueuePut(down ? OS_INPUT_JOYSTICK_BUTTON_DOWN : OS_INPUT_JOYSTICK_BUTTON_UP, button, 0, 0, 0);
        }
    }

    if (index == 0 && g_options.layout != 1) {
        if (joystick->m_pad) {
            EmitPad(joystick, active);
        } else {
            EmitRaw(joystick, state);
        }
    }

    if (joystick->m_pad && active) {
        ApplyRetail(joystick);
    } else if (index == 0 && !joystick->m_pad && DriveAsPad(joystick) && g_options.layout != 1) {
        float dead = g_options.deadzone;
        float lx = active && (joystick->m_axisMask & 1) ? state->m_axes[0] / 32767.0f : 0.0f;
        float ly = active && (joystick->m_axisMask & 2) ? state->m_axes[1] / 32767.0f : 0.0f;
        float rx = active && (joystick->m_axisMask & 4) ? state->m_axes[2] / 32767.0f : 0.0f;
        float ry = active && (joystick->m_axisMask & 8) ? state->m_axes[3] / 32767.0f : 0.0f;
        if (lx < dead && lx > -dead) lx = 0.0f;
        if (ly < dead && ly > -dead) ly = 0.0f;
        if (rx < dead && rx > -dead) rx = 0.0f;
        if (ry < dead && ry > -dead) ry = 0.0f;
        ApplyAxes(joystick, lx, ly, rx, ry);
        ReleasePresetButtons(joystick);
    } else if (index == 0 && joystick->m_pad) {
        ReleasePresetButtons(joystick);
        g_lookX = 0.0f;
        g_lookY = 0.0f;
    }

    for (int32_t i = 0; i < 4; i++) {
        uint32_t pov = state->m_pov[i];

        if (previousState->m_pov[i] != pov) {
            int32_t direction = 0;

            if (pov != 0xFFFFFFFF) {
                direction = 8 * pov / 36000 + 1;
            }

            OsQueuePut(OS_INPUT_JOYSTICK_HAT, i, direction, 0, 0);
        }
    }
}

// OFFSET: 0x008714B0
void OsPollJoysticks(int32_t active) {
    if (!s_joysticks.Count()) {
        return;
    }

    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();

    for (uint32_t i = 0; i < s_joysticks.Count(); i++) {
        PollJoystick(i, active);
    }

    DropJoystickListIfIdle();
}

void OsGamepadCamera(float* x, float* y) {
    if (x) *x = g_cursor ? 0.0f : g_lookX;
    if (y) *y = g_cursor ? 0.0f : g_lookY;
}

int32_t OsGamepadCursorMode() {
    return g_cursor;
}
