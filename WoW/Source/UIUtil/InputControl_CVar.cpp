#include <cstdint>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/FrameXML/XMLTree.h"
#include "Engine/Source/Os/Joystick.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/UIUtil/InputControl.h"

// STATIC: 0x00C2495C
CVar* s_cvCinematicJoystick;

// STATIC: 0x00C24960
CVar* s_cvEnableWowMouse;

// VERIFY: a786a3e1 manual
// OFFSET: 0x005F99F0
void ApplyJoystickConfig(XMLNode* config) {
    if (GetJoystick() == 0xFFFFFFFF) {
        return;
    }

    char name[32];

    for (XMLNode* child = config->m_child; child; child = child->m_next) {
        if (!SStrCmpI(child->m_name.m_str, "stick", STORM_MAX_STR)) {
            int32_t stick = 0;
            int32_t xAxis = 0;
            int32_t yAxis = 1;

            const char* value = child->GetAttributeByName("index");

            if (value) {
                stick = SStrToInt(value) - 1;
            }

            value = child->GetAttributeByName("axisX");

            if (value) {
                xAxis = SStrToInt(value) - 1;
            }

            value = child->GetAttributeByName("axisY");

            if (value) {
                yAxis = SStrToInt(value) - 1;
            }

            OsJoystickMapStick(s_joystick, stick, xAxis, yAxis);
        } else if (!SStrCmpI(child->m_name.m_str, "slider", STORM_MAX_STR)) {
            const char* value = child->GetAttributeByName("axis");

            if (value) {
                OsJoystickMapSlider(s_joystick, SStrToInt(value) - 1);
            }
        } else if (!SStrCmpI(child->m_name.m_str, "hat", STORM_MAX_STR)) {
            int32_t hat = 0;
            int32_t buttons[4] = { -1, -1, -1, -1 };

            const char* value = child->GetAttributeByName("index");

            if (value) {
                hat = SStrToInt(value) - 1;
            }

            for (uint32_t i = 0; i < 4; i++) {
                SStrPrintf(name, sizeof(name), "button%d", i + 1);
                value = child->GetAttributeByName(name);

                if (value) {
                    buttons[i] = SStrToInt(value) - 1;
                }
            }

            OsJoystickMapHat(s_joystick, hat, buttons[0], buttons[1], buttons[2], buttons[3]);
        }
    }

    XMLNode* mouse = config->GetChildByName("mouse");

    if (!mouse) {
        return;
    }

    const char* value = mouse->GetAttributeByName("enabled");

    if (value) {
        s_joystickMouseEnabled = StringToBOOL(value);
    }

    if (!s_joystickMouseEnabled) {
        return;
    }

    float speed = 1.5f;
    int32_t stick = 0;
    int32_t leftButton = 0;
    int32_t rightButton = 1;

    value = mouse->GetAttributeByName("stickIndex");

    if (value) {
        stick = SStrToInt(value) - 1;
    }

    value = mouse->GetAttributeByName("buttonLeft");

    if (value) {
        leftButton = SStrToInt(value) - 1;
    }

    value = mouse->GetAttributeByName("buttonRight");

    if (value) {
        rightButton = SStrToInt(value) - 1;
    }

    value = mouse->GetAttributeByName("speedScale");
    OsJoystickConfigureMouseMode(s_joystick, stick, leftButton, rightButton, value ? SStrToFloat(value) : speed);
}

// VERIFY: ff53c0b8 auto
// OFFSET: 0x005F9C80
bool JoystickCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (*value == '1') {
        if (GetJoystick() == 0xFFFFFFFF) {
            SetJoystick(OsJoystickOpen(0));

            if (s_joystick == 0xFFFFFFFF) {
                return true;
            }
        }

        char name[1024];
        ConsolePrintf("Joystick: %s", OsJoystickGetName(s_joystick, name, sizeof(name)));

        XMLNode* config;
        XMLTree* tree = LoadJoystickConfig(config);

        if (tree) {
            ApplyJoystickConfig(config);
            XMLTree_Free(tree);
        }
    } else if (GetJoystick() != 0xFFFFFFFF) {
        OsJoystickClose(s_joystick);
        SetJoystick(0xFFFFFFFF);
    }

    return true;
}

// VERIFY: 0766c614 auto
// OFFSET: 0x005FA9B0
bool WowMouseCVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    CGInputControl::s_inputControl->SetMouseCVarHandler((value ? SStrToInt(value) : 0) != 0);
    return true;
}
