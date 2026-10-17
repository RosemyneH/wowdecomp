#include "GuildRegistrar.h"
#include <cstdint>
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Game/ValidateName.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/Types.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

// VERIFY: 8986a798 manual
// OFFSET: 0x00599F40
static int32_t Script_GetGuildCharterCost(lua_State* L) {
    lua_pushnumber(L, CGGuildRegistrar::m_petition.m_cost);
    return 1;
}

// VERIFY: e1253c79 auto
// OFFSET: 0x00599F00
static int32_t Script_CloseGuildRegistrar(lua_State* L) {
    if (CGGuildRegistrar::m_registrar) {
        CGGameUI::ClearInteractTarget(CGGuildRegistrar::m_registrar, 0);
        CGGuildRegistrar::m_registrar = WOWGUID();
        FrameScript_SignalEvent(SCRIPT_EVENT_GUILD_REGISTRAR_CLOSED, nullptr);
    }

    return 0;
}

// VERIFY: a9ebbc34 auto
// OFFSET: 0x0059A130
static int32_t Script_BuyGuildCharter(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: BuyGuildCharter(guildName)");
        return 0;
    }

    const char* name = lua_tolstring(L, 1, nullptr);
    VALIDATE_NAME_RESULT error = ValidateGuildName(s_locale, name);

    if (error) {
        CGGameUI::DisplayNameError(error);
        lua_pushnil(L);
    } else {
        CGGuildRegistrar::BuyGuildCharter(name);
        lua_pushnumber(L, 1.0);
    }

    return 1;
}

// VERIFY: bf6ed02f manual
// OFFSET: 0x0059A1C0
static int32_t Script_TurnInGuildCharter(lua_State* L) {
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();

    if (player) {
        player->TurnInGuildCharter();
    }

    return 0;
}

// VERIFY: e557935d manual
// OFFSET: 0x0059A1F0
static int32_t Script_GetTabardInfo(lua_State* L) {
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();

    if (player) {
        WOWGUID registrar = CGGuildRegistrar::m_registrar;
        player->TalkToTabardVendor(registrar);
    }

    if (CGGuildRegistrar::m_registrar) {
        CGGameUI::ClearInteractTarget(CGGuildRegistrar::m_registrar, 0);
        CGGuildRegistrar::m_registrar = WOWGUID();
        FrameScript_SignalEvent(SCRIPT_EVENT_GUILD_REGISTRAR_CLOSED, nullptr);
    }

    return 0;
}

// STATIC: 0x00ACF5FC
static FrameScript_Method s_ScriptFunctions[] = {
    { "CloseGuildRegistrar", &Script_CloseGuildRegistrar },
    { "GetGuildCharterCost", &Script_GetGuildCharterCost },
    { "BuyGuildCharter", &Script_BuyGuildCharter },
    { "TurnInGuildCharter", &Script_TurnInGuildCharter },
    { "GetTabardInfo", &Script_GetTabardInfo },
};

// VERIFY: e5b8e04b auto
// OFFSET: 0x00599F80
void GuildRegistrarRegisterScriptFunctions() {
    for (uint32_t i = 0; i < sizeof(s_ScriptFunctions) / sizeof(s_ScriptFunctions[0]); i++) {
        FrameScript_RegisterFunction(s_ScriptFunctions[i].m_name, s_ScriptFunctions[i].m_method);
    }
}

// VERIFY: ab007013 auto
// OFFSET: 0x00599FB0
void GuildRegistrarUnregisterScriptFunctions() {
    for (uint32_t i = 0; i < sizeof(s_ScriptFunctions) / sizeof(s_ScriptFunctions[0]); i++) {
        FrameScript_UnregisterFunction(s_ScriptFunctions[i].m_name);
    }
}
