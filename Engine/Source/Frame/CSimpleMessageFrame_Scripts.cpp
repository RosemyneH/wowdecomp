#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/CSimpleMessageFrame.h"
#include "Engine/Source/Frame/CSimpleFont.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

// VERIFY: 792a676f auto
// OFFSET: 0x009748D0
void CSimpleMessageFrame::CreateScriptMetaTable() {
    CSimpleMessageFrame::s_metatable = FrameScript_Object::CreateScriptMetaTable(FrameScript_GetContext(), &CSimpleMessageFrame::RegisterScriptMethods);
}

// VERIFY: c87b4319 auto
// OFFSET: 0x00973D40
void CSimpleMessageFrame::DestroyScriptMetaTable() {
    luaL_unref(FrameScript_GetContext(), LUA_REGISTRYINDEX, CSimpleMessageFrame::s_metatable);
    CSimpleMessageFrame::s_metatable = LUA_NOREF;
}

// VERIFY: 2e58f0f2 auto
// OFFSET: 0x00973D70
static int32_t Script_SetFontObject(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetFontObject(name ? name : "<unnamed>", font, L);
}

// VERIFY: 901ab291 auto
// OFFSET: 0x00973DD0
static int32_t Script_GetFontObject(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetFontObject(name ? name : "<unnamed>", font, L);
}

// VERIFY: f96498b1 auto
// OFFSET: 0x00973E30
static int32_t Script_SetFont(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));

    if (!frame || !frame->m_font) {
        return 0;
    }

    const char* name = frame->GetName();
    return CSimpleFont_SetFont(name ? name : "<unnamed>", frame->m_font, L);
}

// VERIFY: 9a29d66b auto
// OFFSET: 0x00973EA0
static int32_t Script_GetFont(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetFont(name ? name : "<unnamed>", font, L);
}

// VERIFY: 175cc51c auto
// OFFSET: 0x00973F00
static int32_t Script_SetTextColor(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetTextColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 537beb20 auto
// OFFSET: 0x00973F60
static int32_t Script_GetTextColor(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetTextColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 3aee71b2 auto
// OFFSET: 0x00973FC0
static int32_t Script_SetShadowColor(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetShadowColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 597183cc auto
// OFFSET: 0x00974020
static int32_t Script_GetShadowColor(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetShadowColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 6cd5145b auto
// OFFSET: 0x00974080
static int32_t Script_SetShadowOffset(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetShadowOffset(name ? name : "<unnamed>", font, L);
}

// VERIFY: 6ebe9800 auto
// OFFSET: 0x009740E0
static int32_t Script_GetShadowOffset(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetShadowOffset(name ? name : "<unnamed>", font, L);
}

// VERIFY: 1178b1d4 auto
// OFFSET: 0x00974140
static int32_t Script_SetSpacing(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetSpacing(name ? name : "<unnamed>", font, L);
}

// VERIFY: 56fcbcca auto
// OFFSET: 0x009741A0
static int32_t Script_GetSpacing(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetSpacing(name ? name : "<unnamed>", font, L);
}

// VERIFY: 40820160 auto
// OFFSET: 0x00974200
static int32_t Script_SetJustifyH(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetJustifyH(name ? name : "<unnamed>", font, L);
}

// VERIFY: c3b14c84 auto
// OFFSET: 0x00974260
static int32_t Script_GetJustifyH(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetJustifyH(name ? name : "<unnamed>", font, L);
}

// VERIFY: f2d42f88 auto
// OFFSET: 0x009742C0
static int32_t Script_SetJustifyV(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetJustifyV(name ? name : "<unnamed>", font, L);
}

// VERIFY: 74e23e98 auto
// OFFSET: 0x00974320
static int32_t Script_GetJustifyV(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetJustifyV(name ? name : "<unnamed>", font, L);
}

// VERIFY: ac8f3e00 auto
// OFFSET: 0x00974380
static int32_t Script_SetInsertMode(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));

    if (lua_isstring(L, 2)) {
        const char* mode = lua_tolstring(L, 2, nullptr);

        if (!SStrCmpI(mode, "TOP", STORM_MAX_STR)) {
            frame->SetInsertMode(INSERT_AT_TOP);
            return 0;
        }

        if (!SStrCmpI(mode, "BOTTOM", STORM_MAX_STR)) {
            frame->SetInsertMode(INSERT_AT_BOTTOM);
            return 0;
        }
    }

    const char* name = frame->GetName();
    luaL_error(L, "Usage: %s:SetInsertMode(\"TOP\" or \"BOTTOM\")", name ? name : "<unnamed>");
    return 0;
}

// VERIFY: d1739458 auto
// OFFSET: 0x00974440
static int32_t Script_GetInsertMode(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    const char* mode = nullptr;

    switch (frame->m_insertMode) {
        case INSERT_AT_TOP:
            mode = "TOP";
            break;
        case INSERT_AT_BOTTOM:
            lua_pushstring(L, "BOTTOM");
            return 1;
    }

    lua_pushstring(L, mode);
    return 1;
}

// VERIFY: 15cc6cbe auto
// OFFSET: 0x009744B0
static int32_t Script_SetIndentedWordWrap(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetIndentedWordWrap(name ? name : "<unnamed>", font, L);
}

// VERIFY: dfa42553 auto
// OFFSET: 0x00974510
static int32_t Script_GetIndentedWordWrap(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetIndentedWordWrap(name ? name : "<unnamed>", font, L);
}

// VERIFY: 5488bb1e manual
// OFFSET: 0x00974570
static int32_t Script_SetFading(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    frame->m_fading = StringToBOOL(L, 2, 1);
    return 0;
}

// VERIFY: 8cf4224d auto
// OFFSET: 0x009745C0
static int32_t Script_GetFading(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));

    if (frame->m_fading) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// VERIFY: ba450199 auto
// OFFSET: 0x00974620
static int32_t Script_SetTimeVisible(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));

    if (lua_isnumber(L, 2)) {
        frame->SetTimeVisible(static_cast<float>(lua_tonumber(L, 2)));
        return 0;
    }

    const char* name = frame->GetName();
    luaL_error(L, "Usage: %s:SetTimeVisible(seconds)", name ? name : "<unnamed>");
    return 0;
}

// VERIFY: e091f24f manual
// OFFSET: 0x009746A0
static int32_t Script_GetTimeVisible(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_timeVisible);
    return 1;
}

// VERIFY: 3a16b026 auto
// OFFSET: 0x009746F0
static int32_t Script_SetFadeDuration(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));

    if (lua_isnumber(L, 2)) {
        frame->SetFadeDuration(static_cast<float>(lua_tonumber(L, 2)));
        return 0;
    }

    const char* name = frame->GetName();
    luaL_error(L, "Usage: %s:SetFadeDuration(seconds)", name ? name : "<unnamed>");
    return 0;
}

// VERIFY: 9b9ef855 manual
// OFFSET: 0x00974770
static int32_t Script_GetFadeDuration(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_fadeDuration);
    return 1;
}

// VERIFY: 868b3db9 manual
// OFFSET: 0x009747C0
static int32_t Script_AddMessage(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));

    if (lua_isstring(L, 2)) {
        const char* text = lua_tolstring(L, 2, nullptr);

        if (text && *text) {
            CImVector color;
            color.b = 0xFF;
            color.g = 0xFF;
            color.r = 0xFF;
            color.a = 0xFF;

            if (lua_isnumber(L, 3) && lua_isnumber(L, 4) && lua_isnumber(L, 5)) {
                FrameScript_GetColor(L, 3, color);
            }

            frame->AddMessage(text, color);
        }
    }

    return 0;
}

// VERIFY: c4cc41a4 manual
// OFFSET: 0x00974890
static int32_t Script_Clear(lua_State* L) {
    CSimpleMessageFrame* frame = static_cast<CSimpleMessageFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageFrame::GetObjectType()));
    frame->Clear();
    return 0;
}

// STATIC: 0x00B2D068
static FrameScript_Method s_ScriptMethods[] = {
    { "SetFontObject", &Script_SetFontObject },
    { "GetFontObject", &Script_GetFontObject },
    { "SetFont", &Script_SetFont },
    { "GetFont", &Script_GetFont },
    { "SetTextColor", &Script_SetTextColor },
    { "GetTextColor", &Script_GetTextColor },
    { "SetShadowColor", &Script_SetShadowColor },
    { "GetShadowColor", &Script_GetShadowColor },
    { "SetShadowOffset", &Script_SetShadowOffset },
    { "GetShadowOffset", &Script_GetShadowOffset },
    { "SetSpacing", &Script_SetSpacing },
    { "GetSpacing", &Script_GetSpacing },
    { "SetJustifyH", &Script_SetJustifyH },
    { "GetJustifyH", &Script_GetJustifyH },
    { "SetJustifyV", &Script_SetJustifyV },
    { "GetJustifyV", &Script_GetJustifyV },
    { "SetInsertMode", &Script_SetInsertMode },
    { "GetInsertMode", &Script_GetInsertMode },
    { "SetIndentedWordWrap", &Script_SetIndentedWordWrap },
    { "GetIndentedWordWrap", &Script_GetIndentedWordWrap },
    { "SetFading", &Script_SetFading },
    { "GetFading", &Script_GetFading },
    { "SetTimeVisible", &Script_SetTimeVisible },
    { "GetTimeVisible", &Script_GetTimeVisible },
    { "SetFadeDuration", &Script_SetFadeDuration },
    { "GetFadeDuration", &Script_GetFadeDuration },
    { "AddMessage", &Script_AddMessage },
    { "Clear", &Script_Clear },
};

// VERIFY: 4b0e9196 auto
// OFFSET: 0x00973D20
void CSimpleMessageFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, s_ScriptMethods, sizeof(s_ScriptMethods) / sizeof(s_ScriptMethods[0]));
}
