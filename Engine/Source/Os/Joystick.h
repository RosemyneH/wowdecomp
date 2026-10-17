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

#if defined(WOWDECOMP_LINUX)
void OsGamepadCamera(float* x, float* y);
int32_t OsGamepadCursorMode();
#else
inline void OsGamepadCamera(float* x, float* y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
}
inline int32_t OsGamepadCursorMode() { return 0; }
#endif
void OsPollJoysticks(int32_t active);

#endif
