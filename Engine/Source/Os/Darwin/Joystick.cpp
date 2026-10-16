#include <cmath>
#include <cstdint>
#include <cstring>
#include <SDL.h>
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/Tempest/C2Vector.h"

static const float s_axisMin = -32768.0f;
static const float s_axisMax = 32767.0f;

struct MacJoystickStick {
    int32_t m_xAxis;
    int32_t m_yAxis;
};

struct MacJoystickHat {
    bool m_enabled;
    int32_t m_buttons[4];
};

struct MacJoystickMouseMode {
    int32_t m_enabled;
    int32_t m_stick;
    float m_speed;
    int32_t m_leftButton;
    int32_t m_rightButton;
    uint32_t m_lastTime;
    C2Vector m_accumulated;
};

struct MacJoystickState {
    int32_t m_axes[8];
    uint8_t m_buttons[32];
    uint32_t m_pov[4];
};

struct MacJoystick {
    int32_t m_deviceIndex;
    SDL_Joystick* m_device;
    uint32_t m_axisMask;
    MacJoystickStick m_sticks[4];
    bool m_stickAxis[8];
    bool m_sliderAxis[8];
    MacJoystickHat m_hats[4];
    bool m_hatButton[32];
    MacJoystickMouseMode m_mouseMode;
    uint32_t m_stateIndex;
    MacJoystickState m_states[2];
    int32_t m_axes[2][8];
};

static MacJoystickMouseMode s_defaultMouseMode = { 1, 0, 1.5f, 0, 1, 0 };

static int32_t s_initialized;
static uint32_t s_listId;

static TSGrowableArray<MacJoystick> s_joysticks;

static void ResetState(MacJoystickState* state) {
    memset(state, 0, sizeof(MacJoystickState));

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

    MacJoystick* joystick = &s_joysticks[index];

    if (stick >= 0 && stick < 4 && xAxis >= 0 && xAxis < 8 && (joystick->m_axisMask & (1 << xAxis)) && yAxis >= 0 && yAxis < 8 && (joystick->m_axisMask & (1 << yAxis))) {
        joystick->m_sticks[stick].m_xAxis = xAxis;
        joystick->m_sticks[stick].m_yAxis = yAxis;
        joystick->m_stickAxis[xAxis] = true;
        joystick->m_stickAxis[yAxis] = true;
    }
}

static bool AnyJoystickOpen() {
    for (uint32_t i = 0; i < s_joysticks.Count(); i++) {
        if (s_joysticks[i].m_device) {
            return true;
        }
    }

    return false;
}

static bool ReplugAlreadyPickedUp() {
    int32_t live = SDL_NumJoysticks();

    if (live < 0 || static_cast<uint32_t>(live) != s_joysticks.Count()) {
        return false;
    }

    for (uint32_t i = 0; i < s_joysticks.Count(); i++) {
        const MacJoystick* joystick = &s_joysticks[i];

        if (joystick->m_device || joystick->m_deviceIndex != static_cast<int32_t>(i) || joystick->m_axisMask) {
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
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
            return 0;
        }

        s_initialized = 1;
        s_listId++;

        SDL_JoystickEventState(SDL_IGNORE);

        int32_t count = SDL_NumJoysticks();

        for (int32_t i = 0; i < count; i++) {
            MacJoystick* joystick = s_joysticks.New();
            memset(joystick, 0, sizeof(MacJoystick));
            joystick->m_deviceIndex = i;
            joystick->m_mouseMode = s_defaultMouseMode;

            for (int32_t stick = 0; stick < 4; stick++) {
                joystick->m_sticks[stick].m_xAxis = -1;
                joystick->m_sticks[stick].m_yAxis = -1;
            }
        }
    }

    return s_joysticks.Count();
}

uint32_t OsJoystickOpen(uint32_t index) {
    if (index >= OsJoystickCount()) {
        return 0xFFFFFFFF;
    }

    MacJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device) {
        joystick->m_device = SDL_JoystickOpen(joystick->m_deviceIndex);

        if (!joystick->m_device) {
            return 0xFFFFFFFF;
        }
    }

    int32_t axes = SDL_JoystickNumAxes(joystick->m_device);
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

    MacJoystick* joystick = &s_joysticks[index];

    if (joystick->m_device) {
        SDL_JoystickClose(joystick->m_device);
        joystick->m_device = nullptr;
    }
}

char* OsJoystickGetName(uint32_t index, char* buffer, uint32_t size) {
    if (index >= OsJoystickCount()) {
        return nullptr;
    }

    MacJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device) {
        return nullptr;
    }

    const char* name = SDL_JoystickName(joystick->m_device);
    SStrCopy(buffer, name ? name : "", size);

    return buffer;
}

// OFFSET: 0x008708D0
void OsJoystickMapSlider(uint32_t index, int32_t axis) {
    if (index >= OsJoystickCount()) {
        return;
    }

    MacJoystick* joystick = &s_joysticks[index];

    if (joystick->m_device && axis >= 0 && axis < 8) {
        joystick->m_sliderAxis[axis] = true;
    }
}

// OFFSET: 0x00870910
void OsJoystickMapHat(uint32_t index, int32_t hat, int32_t up, int32_t right, int32_t down, int32_t left) {
    if (index >= OsJoystickCount()) {
        return;
    }

    MacJoystick* joystick = &s_joysticks[index];

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

    MacJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device) {
        return;
    }

    MacJoystickMouseMode* mouseMode = &joystick->m_mouseMode;

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

    MacJoystickState* state = &joystick->m_states[joystick->m_stateIndex];
    int32_t* axes = joystick->m_axes[joystick->m_stateIndex];
    MacJoystickStick* stick = &joystick->m_sticks[mouseMode->m_stick];

    if (stick->m_xAxis >= 0 && stick->m_yAxis >= 0 && (axes[stick->m_xAxis] || axes[stick->m_yAxis])) {
        OsQueuePut(OS_INPUT_14, mouseMode->m_stick, 0, 0, 0);
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

    MacJoystick* joystick = &s_joysticks[index];

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

static void ReadState(MacJoystick* joystick, MacJoystickState* state) {
    SDL_Joystick* device = joystick->m_device;

    for (int32_t axis = 0; axis < 8; axis++) {
        state->m_axes[axis] = joystick->m_axisMask & (1 << axis) ? SDL_JoystickGetAxis(device, axis) : 0;
    }

    int32_t buttons = SDL_JoystickNumButtons(device);

    for (int32_t button = 0; button < 32; button++) {
        state->m_buttons[button] = button < buttons ? SDL_JoystickGetButton(device, button) : 0;
    }

    int32_t hats = SDL_JoystickNumHats(device);

    for (int32_t hat = 0; hat < 4; hat++) {
        state->m_pov[hat] = hat < hats ? HatToPov(SDL_JoystickGetHat(device, hat)) : 0xFFFFFFFF;
    }
}

// OFFSET: 0x00870B80
static void PollJoystick(uint32_t index, int32_t active) {
    MacJoystick* joystick = &s_joysticks[index];

    if (!joystick->m_device) {
        return;
    }

    if (!SDL_JoystickGetAttached(joystick->m_device)) {
        OsJoystickClose(index);
        return;
    }

    uint32_t previous = joystick->m_stateIndex;
    uint32_t current = previous == 0;
    joystick->m_stateIndex = current;

    MacJoystickState* previousState = &joystick->m_states[previous];
    MacJoystickState* state = &joystick->m_states[current];
    int32_t* previousAxes = joystick->m_axes[previous];
    int32_t* axes = joystick->m_axes[current];

    if (active && SDL_JoystickGetAttached(joystick->m_device)) {
        ReadState(joystick, state);
    } else {
        ResetState(state);
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
        MacJoystickHat* hat = &joystick->m_hats[i];

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

    MacJoystickMouseMode* mouseMode = &joystick->m_mouseMode;

    if (mouseMode->m_enabled && joystick->m_sticks[0].m_xAxis >= 0) {
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
                OsQueuePut(OS_INPUT_14, i, axes[xAxis], axes[yAxis], 0);
            }
        }
    }

    MacJoystickStick* mouseStick = &joystick->m_sticks[mouseMode->m_stick];

    for (int32_t axis = 0; axis < 8; axis++) {
        if ((joystick->m_axisMask & (1 << axis)) && !joystick->m_stickAxis[axis]) {
            if (!mouseMode->m_enabled || (axis != mouseStick->m_xAxis && axis != mouseStick->m_yAxis)) {
                if (previousAxes[axis] != axes[axis]) {
                    OsQueuePut(OS_INPUT_15, axis, axes[axis], 0, 0);
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
            OsQueuePut(down ? OS_INPUT_16 : OS_INPUT_17, button, 0, 0, 0);
        }
    }

    for (int32_t i = 0; i < 4; i++) {
        uint32_t pov = state->m_pov[i];

        if (previousState->m_pov[i] != pov) {
            int32_t direction = 0;

            if (pov != 0xFFFFFFFF) {
                direction = 8 * pov / 36000 + 1;
            }

            OsQueuePut(OS_INPUT_18, i, direction, 0, 0);
        }
    }
}

// OFFSET: 0x008714B0
void OsPollJoysticks(int32_t active) {
    if (!s_joysticks.Count()) {
        return;
    }

    SDL_JoystickUpdate();

    for (uint32_t i = 0; i < s_joysticks.Count(); i++) {
        PollJoystick(i, active);
    }

    DropJoystickListIfIdle();
}
