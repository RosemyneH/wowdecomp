#ifndef ENGINE_SOURCE_OS_JOYSTICK_H
#define ENGINE_SOURCE_OS_JOYSTICK_H

#include <cstdint>

void OsJoystickClose(uint32_t index);
void OsJoystickConfigureMouseMode(uint32_t index, int32_t stick, int32_t leftButton, int32_t rightButton, float speed);
uint32_t OsJoystickCount();
uint32_t OsJoystickListId();
char* OsJoystickGetName(uint32_t index, char* buffer, uint32_t size);
void OsJoystickMapHat(uint32_t index, int32_t hat, int32_t up, int32_t right, int32_t down, int32_t left);
void OsJoystickMapSlider(uint32_t index, int32_t axis);
void OsJoystickMapStick(uint32_t index, int32_t stick, int32_t xAxis, int32_t yAxis);
uint32_t OsJoystickOpen(uint32_t index);
void OsJoystickSetMouseMode(uint32_t index, int32_t enable);

struct OsGamepadOptions {
    int32_t layout;
    float deadzone;
    float lookSens;
    float cursorSpeed;
    int32_t invertY;
    int32_t strafe;
    int32_t south;
    int32_t east;
    int32_t west;
    int32_t north;
    int32_t shoulders;
    int32_t triggers;
    int32_t back;
    int32_t start;
    int32_t cursorToggle;
    int32_t autorun;
    int32_t dpad;
};

#if defined(WOWDECOMP_LINUX) || defined(WOWDECOMP_ANDROID)
void OsGamepadCamera(float* x, float* y);
int32_t OsGamepadCursorMode();
void OsGamepadSetOptions(const OsGamepadOptions* options);
const char* OsGamepadConnectedName();
#else
inline void OsGamepadCamera(float* x, float* y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
}
inline int32_t OsGamepadCursorMode() { return 0; }
inline void OsGamepadSetOptions(const OsGamepadOptions*) {}
inline const char* OsGamepadConnectedName() { return ""; }
#endif
void OsPollJoysticks(int32_t active);

#endif
