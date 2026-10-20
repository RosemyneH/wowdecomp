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
    int32_t axisBindings;
    int32_t style;
};

enum {
    OS_GAMEPAD_NONE = 0,
    OS_GAMEPAD_STANDARD = 1,
    OS_GAMEPAD_GAMECUBE = 2,
};

struct OsGamepadEdge {
    char key[24];
    int32_t down;
};

#if defined(WOWDECOMP_LINUX) || defined(WOWDECOMP_ANDROID)
void OsGamepadCamera(float* x, float* y);
int32_t OsGamepadCursorMode();
void OsGamepadSetOptions(const OsGamepadOptions* options);
const char* OsGamepadConnectedName();
int32_t OsGamepadKind();
int32_t OsGamepadMapped();
int32_t OsGamepadControlCount();
int32_t OsGamepadControl(int32_t index, char* key, uint32_t keySize, char* label, uint32_t labelSize, int32_t* down);
int32_t OsGamepadDrainEdges(OsGamepadEdge* out, int32_t max);
const char* OsGamepadPopCapture();
void OsGamepadSetCapture(int32_t on);
int32_t OsGamepadCapture();
#else
inline void OsGamepadCamera(float* x, float* y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
}
inline int32_t OsGamepadCursorMode() { return 0; }
inline void OsGamepadSetOptions(const OsGamepadOptions*) {}
inline const char* OsGamepadConnectedName() { return ""; }
inline int32_t OsGamepadKind() { return OS_GAMEPAD_NONE; }
inline int32_t OsGamepadMapped() { return 0; }
inline int32_t OsGamepadControlCount() { return 0; }
inline int32_t OsGamepadControl(int32_t, char*, uint32_t, char*, uint32_t, int32_t* down) {
    if (down) *down = 0;
    return 0;
}
inline int32_t OsGamepadDrainEdges(OsGamepadEdge*, int32_t) { return 0; }
inline const char* OsGamepadPopCapture() { return ""; }
inline void OsGamepadSetCapture(int32_t) {}
inline int32_t OsGamepadCapture() { return 0; }
#endif
void OsPollJoysticks(int32_t active);

#endif
