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
static int32_t PadFlag(const char* name, int32_t fallback) {
    CVar* var = CVar::Lookup(name);
    return var ? var->m_intValue : fallback;
}

static float PadAmount(const char* name, float fallback) {
    CVar* var = CVar::Lookup(name);
    return var ? var->m_floatValue : fallback;
}

static void PushGamepadOptions() {
    OsGamepadOptions options;
    options.layout = PadFlag("padLayout", 0);
    options.deadzone = PadAmount("padDeadzone", 0.18f);
    options.lookSens = PadAmount("padLookSens", 1.0f);
    options.cursorSpeed = PadAmount("padCursorSpeed", 16.0f);
    options.invertY = PadFlag("padInvertY", 0);
    options.strafe = PadFlag("padStrafe", 1);
    options.south = PadFlag("padSouth", 1);
    options.east = PadFlag("padEast", 1);
    options.west = PadFlag("padWest", 1);
    options.north = PadFlag("padNorth", 1);
    options.shoulders = PadFlag("padShoulders", 1);
    options.triggers = PadFlag("padTriggers", 1);
    options.back = PadFlag("padBack", 1);
    options.start = PadFlag("padStart", 1);
    options.cursorToggle = PadFlag("padCursorToggle", 1);
    options.autorun = PadFlag("padAutorun", 1);
    options.dpad = PadFlag("padDpad", 1);
    OsGamepadSetOptions(&options);
}

static bool PadOptionsCallback(CVar*, const char*, const char*, void*) {
    PushGamepadOptions();
    return true;
}

static void RegisterPadCVar(const char* name, const char* help, const char* value) {
    CVar::Register(name, help, 0, value, PadOptionsCallback, 5, false, nullptr, false);
}

void RegisterGamepadCVars() {
    RegisterPadCVar("padLayout", "0 retail gamepad layout, 1 Steam Input passthrough", "0");
    RegisterPadCVar("padDeadzone", "gamepad stick deadzone", "0.18");
    RegisterPadCVar("padLookSens", "gamepad look sensitivity", "1");
    RegisterPadCVar("padCursorSpeed", "gamepad cursor speed", "16");
    RegisterPadCVar("padInvertY", "invert gamepad look", "0");
    RegisterPadCVar("padStrafe", "left stick strafes instead of turning", "1");
    RegisterPadCVar("padSouth", "A jumps, or clicks while the cursor is out", "1");
    RegisterPadCVar("padEast", "B cancels", "1");
    RegisterPadCVar("padWest", "X interacts", "1");
    RegisterPadCVar("padNorth", "Y sheathes", "1");
    RegisterPadCVar("padShoulders", "shoulders change target", "1");
    RegisterPadCVar("padTriggers", "triggers press action bar slots 1 and 2", "1");
    RegisterPadCVar("padBack", "Select opens the map", "1");
    RegisterPadCVar("padStart", "Start opens the menu", "1");
    RegisterPadCVar("padCursorToggle", "right stick click toggles the cursor", "1");
    RegisterPadCVar("padAutorun", "left stick click toggles autorun", "1");
    RegisterPadCVar("padDpad", "d-pad opens character, bags, spellbook, and quests", "1");
    PushGamepadOptions();
}

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
