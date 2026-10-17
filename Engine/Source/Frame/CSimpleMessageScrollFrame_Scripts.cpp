#include <cstddef>
#include <cstdint>
#include "Engine/Source/Frame/CSimpleMessageScrollFrame.h"
#include "Engine/Source/Frame/CSimpleFont.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/FrameXML/LoadXML.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

// VERIFY: b3f5711e auto
// OFFSET: 0x00973D00
void CSimpleMessageScrollFrame::CreateScriptMetaTable() {
    CSimpleMessageScrollFrame::s_metatable = FrameScript_Object::CreateScriptMetaTable(FrameScript_GetContext(), &CSimpleMessageScrollFrame::RegisterScriptMethods);
}

// VERIFY: be4532f0 auto
// OFFSET: 0x00972830
void CSimpleMessageScrollFrame::DestroyScriptMetaTable() {
    luaL_unref(FrameScript_GetContext(), LUA_REGISTRYINDEX, CSimpleMessageScrollFrame::s_metatable);
    CSimpleMessageScrollFrame::s_metatable = LUA_NOREF;
}

// VERIFY: f1c40ef5 auto
// OFFSET: 0x00972860
static int32_t Script_SetFontObject(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetFontObject(name ? name : "<unnamed>", font, L);
}

// VERIFY: f32c8c4a auto
// OFFSET: 0x009728C0
static int32_t Script_GetFontObject(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetFontObject(name ? name : "<unnamed>", font, L);
}

// VERIFY: 897ad1a7 auto
// OFFSET: 0x00972920
static int32_t Script_SetFont(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetFont(name ? name : "<unnamed>", font, L);
}

// VERIFY: 13650b27 auto
// OFFSET: 0x00972980
static int32_t Script_GetFont(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetFont(name ? name : "<unnamed>", font, L);
}

// VERIFY: c58098c2 auto
// OFFSET: 0x009729E0
static int32_t Script_SetTextColor(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetTextColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 7992ec1a auto
// OFFSET: 0x00972A40
static int32_t Script_GetTextColor(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetTextColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: d9068ba5 auto
// OFFSET: 0x00972AA0
static int32_t Script_SetShadowColor(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetShadowColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 2a6af55a auto
// OFFSET: 0x00972B00
static int32_t Script_GetShadowColor(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetShadowColor(name ? name : "<unnamed>", font, L);
}

// VERIFY: 57abaa8c auto
// OFFSET: 0x00972B60
static int32_t Script_SetShadowOffset(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetShadowOffset(name ? name : "<unnamed>", font, L);
}

// VERIFY: 97df6f6a auto
// OFFSET: 0x00972BC0
static int32_t Script_GetShadowOffset(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetShadowOffset(name ? name : "<unnamed>", font, L);
}

// VERIFY: 9921aa0a auto
// OFFSET: 0x00972C20
static int32_t Script_SetSpacing(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetSpacing(name ? name : "<unnamed>", font, L);
}

// VERIFY: 9d7ae4f0 auto
// OFFSET: 0x00972C80
static int32_t Script_GetSpacing(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetSpacing(name ? name : "<unnamed>", font, L);
}

// VERIFY: 982be581 auto
// OFFSET: 0x00972CE0
static int32_t Script_SetJustifyH(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetJustifyH(name ? name : "<unnamed>", font, L);
}

// VERIFY: 8cc765d1 auto
// OFFSET: 0x00972D40
static int32_t Script_GetJustifyH(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetJustifyH(name ? name : "<unnamed>", font, L);
}

// VERIFY: 09435a77 auto
// OFFSET: 0x00972DA0
static int32_t Script_SetJustifyV(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetJustifyV(name ? name : "<unnamed>", font, L);
}

// VERIFY: de7dc35b auto
// OFFSET: 0x00972E00
static int32_t Script_GetJustifyV(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetJustifyV(name ? name : "<unnamed>", font, L);
}

// VERIFY: 0dee1d63 auto
// OFFSET: 0x00972E60
static int32_t Script_SetIndentedWordWrap(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_SetIndentedWordWrap(name ? name : "<unnamed>", font, L);
}

// VERIFY: 7bce2cea auto
// OFFSET: 0x00972EC0
static int32_t Script_GetIndentedWordWrap(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    CSimpleFont* font = frame->m_font;
    const char* name = frame->GetName();
    return CSimpleFont_GetIndentedWordWrap(name ? name : "<unnamed>", font, L);
}

// VERIFY: 44f3a3e6 auto
// OFFSET: 0x00972F20
static int32_t Script_AddMessage(lua_State* L) {
    int32_t extraData = 0;
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (!lua_isstring(L, 2)) {
        const char* name = frame->GetName();
        luaL_error(L, "Usage: %s:AddMessage(\"text\", [r, g, b,] typeID, backFill, accessID, extraData)", name ? name : "<unnamed>");
        return 0;
    }

    const char* text = lua_tolstring(L, 2, nullptr);

    if (!text || !*text) {
        return 0;
    }

    int32_t typeID = 0;
    int32_t accessID = 0;
    CImVector color;
    int32_t addToTop = 0;

    if (lua_isnumber(L, 3) && lua_isnumber(L, 4) && lua_isnumber(L, 5)) {
        FrameScript_GetColorNoAlpha(L, 3, color);

        if (lua_isnumber(L, 6)) {
            typeID = static_cast<int32_t>(lua_tonumber(L, 6));
        }

        if (lua_type(L, 7) == LUA_TBOOLEAN) {
            addToTop = lua_toboolean(L, 7);
        }

        if (lua_isnumber(L, 8)) {
            accessID = static_cast<int32_t>(lua_tonumber(L, 8));
        }

        if (lua_isnumber(L, 9)) {
            extraData = static_cast<int32_t>(lua_tonumber(L, 9));
        }
    } else {
        color = frame->m_font->m_attributes.m_color;

        if (lua_isnumber(L, 3)) {
            typeID = static_cast<int32_t>(lua_tonumber(L, 3));
        }

        if (lua_type(L, 4) == LUA_TBOOLEAN) {
            addToTop = lua_toboolean(L, 4);
        }

        if (lua_isnumber(L, 5)) {
            accessID = static_cast<int32_t>(lua_tonumber(L, 5));
        }

        if (lua_isnumber(L, 6)) {
            extraData = static_cast<int32_t>(lua_tonumber(L, 6));
        }
    }

    frame->AddMessage(text, color, typeID, addToTop, accessID, extraData);
    return 0;
}

// VERIFY: 0d922d7a manual
// OFFSET: 0x00973150
static int32_t Script_GetMessageInfo(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (!lua_isnumber(L, 2)) {
        const char* name = frame->GetName();
        luaL_error(L, "Usage: %s:GetMessageInfo(index[, accessID])", name ? name : "<unnamed>");
        return 0;
    }

    uint32_t index = static_cast<uint32_t>(lua_tonumber(L, 2)) - 1;
    int32_t accessID = lua_tointeger(L, 3);
    CSimpleMessageScrollFrameLine* line = frame->GetMessageInfoByAccessID(index, accessID);

    if (!line) {
        const char* name = frame->GetName();
        luaL_error(L, "%s:GetMessageInfo - A line with accessID %d and index %d does not exist.", name ? name : "<unnamed>", accessID, index);
    }

    lua_pushstring(L, line->m_text);
    lua_pushnumber(L, line->m_accessID);
    lua_pushnumber(L, line->m_typeID);
    lua_pushnumber(L, line->m_extraData);
    return 4;
}

// VERIFY: d590ea90 auto
// OFFSET: 0x00973270
static int32_t Script_RemoveMessagesByAccessID(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (!lua_isnumber(L, 2)) {
        const char* name = frame->GetName();
        luaL_error(L, "Usage: %s:RemoveMessagesByAccessID(accessID)", name ? name : "<unnamed>");
        return 0;
    }

    frame->RemoveLinesByAccessID(lua_tointeger(L, 2));
    return 0;
}

// VERIFY: aa8ba3bc manual
// OFFSET: 0x009732F0
static int32_t Script_ScrollUp(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->ScrollUp();
    return 0;
}

// VERIFY: 1da29d5c manual
// OFFSET: 0x00973330
static int32_t Script_ScrollDown(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->ScrollDown();
    return 0;
}

// VERIFY: 7c31657c manual
// OFFSET: 0x00973370
static int32_t Script_PageUp(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->PageUp();
    return 0;
}

// VERIFY: 3fc564c7 manual
// OFFSET: 0x009733B0
static int32_t Script_PageDown(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->PageDown();
    return 0;
}

// VERIFY: ff2bf1b5 manual
// OFFSET: 0x009733F0
static int32_t Script_ScrollToTop(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->ScrollToTop();
    return 0;
}

// VERIFY: c4e6e053 manual
// OFFSET: 0x00973430
static int32_t Script_ScrollToBottom(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->ScrollToBottom();
    return 0;
}

// VERIFY: c334846d auto
// OFFSET: 0x00973470
static int32_t Script_SetScrollOffset(lua_State* L) {
    if (lua_isnumber(L, 2)) {
        CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
        frame->SetScrollOffset(static_cast<int32_t>(lua_tonumber(L, 2)));
    }

    return 0;
}

// VERIFY: 60a5dcf7 auto
// OFFSET: 0x009734D0
static int32_t Script_AtTop(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (frame->m_atTop) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// VERIFY: 7be69ec6 auto
// OFFSET: 0x00973530
static int32_t Script_AtBottom(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (frame->m_atBottom) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// VERIFY: ee12df76 auto
// OFFSET: 0x00973590
static int32_t Script_UpdateColorByID(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4) && lua_isnumber(L, 5)) {
        int32_t typeID = static_cast<int32_t>(lua_tonumber(L, 2));
        CImVector color;
        FrameScript_GetColorNoAlpha(L, 3, color);
        frame->UpdateColorByID(typeID, color);
    }

    return 0;
}

// VERIFY: 301b3f14 auto
// OFFSET: 0x00973640
static int32_t Script_GetNumMessages(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    int32_t accessID = lua_tointeger(L, 2);
    lua_pushnumber(L, frame->GetNumMessages(accessID));
    return 1;
}

// VERIFY: 3f327318 manual
// OFFSET: 0x009736A0
static int32_t Script_GetNumLinesDisplayed(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_numDisplayed);
    return 1;
}

// VERIFY: 0f866f97 manual
// OFFSET: 0x009736F0
static int32_t Script_GetCurrentScroll(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    lua_pushnumber(L, frame->GetCurrentScroll());
    return 1;
}

// VERIFY: b0d83ca9 manual
// OFFSET: 0x00973750
static int32_t Script_GetCurrentLine(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_currentLine);
    return 1;
}

// VERIFY: 1aa51d9a manual
// OFFSET: 0x009737A0
static int32_t Script_GetMaxLines(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_maxMessages);
    return 1;
}

// VERIFY: 35ae8313 auto
// OFFSET: 0x009737F0
static int32_t Script_SetMaxLines(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (lua_isnumber(L, 2)) {
        int32_t maxLines = static_cast<int32_t>(lua_tonumber(L, 2));

        if (maxLines > 0) {
            frame->SetMaxLines(maxLines);
            return 0;
        }
    }

    const char* name = frame->GetName();
    luaL_error(L, "Usage: %s:SetMaxLines(maxLines)", name ? name : "<unnamed>");
    return 0;
}

// VERIFY: a23aed3e manual
// OFFSET: 0x00973880
static int32_t Script_SetFading(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->m_fading = StringToBOOL(L, 2, 1);
    return 0;
}

// VERIFY: f2a10724 auto
// OFFSET: 0x009738D0
static int32_t Script_GetFading(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (frame->m_fading) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// VERIFY: 63cf1ed6 auto
// OFFSET: 0x00973930
static int32_t Script_SetTimeVisible(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (lua_isnumber(L, 2)) {
        frame->SetTimeVisible(static_cast<float>(lua_tonumber(L, 2)));
        return 0;
    }

    const char* name = frame->GetName();
    luaL_error(L, "Usage: %s:SetTimeVisible(seconds)", name ? name : "<unnamed>");
    return 0;
}

// VERIFY: 54e88605 manual
// OFFSET: 0x009739B0
static int32_t Script_GetTimeVisible(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_timeVisible);
    return 1;
}

// VERIFY: aa85eb8c auto
// OFFSET: 0x00973A00
static int32_t Script_SetFadeDuration(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (lua_isnumber(L, 2)) {
        frame->SetFadeDuration(static_cast<float>(lua_tonumber(L, 2)));
        return 0;
    }

    const char* name = frame->GetName();
    luaL_error(L, "Usage: %s:SetFadeDuration(seconds)", name ? name : "<unnamed>");
    return 0;
}

// VERIFY: 292c328c manual
// OFFSET: 0x00973A80
static int32_t Script_GetFadeDuration(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    lua_pushnumber(L, frame->m_fadeDuration);
    return 1;
}

// VERIFY: a6b83570 manual
// OFFSET: 0x00973AD0
static int32_t Script_Clear(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    frame->Clear();
    return 0;
}

// VERIFY: 0481b2bd auto
// OFFSET: 0x00973B10
static int32_t Script_SetInsertMode(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

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

// VERIFY: d6aaf5f0 auto
// OFFSET: 0x00973BD0
static int32_t Script_GetInsertMode(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
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

// VERIFY: 7ab9f69b auto
// OFFSET: 0x00973C40
static int32_t Script_SetHyperlinksEnabled(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));
    bool enabled = StringToBOOL(L, 2, 1) != 0;

    if (enabled != frame->m_hyperlinksEnabled) {
        frame->m_hyperlinksEnabled = enabled;
        frame->RefreshHyperlinks();
    }

    return 0;
}

// VERIFY: 7cf031ac auto
// OFFSET: 0x00973CA0
static int32_t Script_GetHyperlinksEnabled(lua_State* L) {
    CSimpleMessageScrollFrame* frame = static_cast<CSimpleMessageScrollFrame*>(FrameScript_GetObjectThis(L, CSimpleMessageScrollFrame::GetObjectType()));

    if (frame->m_hyperlinksEnabled) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// STATIC: 0x00B2CEE0
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
    { "AddMessage", &Script_AddMessage },
    { "GetMessageInfo", &Script_GetMessageInfo },
    { "RemoveMessagesByAccessID", &Script_RemoveMessagesByAccessID },
    { "ScrollUp", &Script_ScrollUp },
    { "ScrollDown", &Script_ScrollDown },
    { "PageUp", &Script_PageUp },
    { "PageDown", &Script_PageDown },
    { "ScrollToTop", &Script_ScrollToTop },
    { "ScrollToBottom", &Script_ScrollToBottom },
    { "SetScrollOffset", &Script_SetScrollOffset },
    { "AtTop", &Script_AtTop },
    { "AtBottom", &Script_AtBottom },
    { "UpdateColorByID", &Script_UpdateColorByID },
    { "GetNumMessages", &Script_GetNumMessages },
    { "GetNumLinesDisplayed", &Script_GetNumLinesDisplayed },
    { "GetCurrentScroll", &Script_GetCurrentScroll },
    { "GetCurrentLine", &Script_GetCurrentLine },
    { "GetMaxLines", &Script_GetMaxLines },
    { "SetMaxLines", &Script_SetMaxLines },
    { "SetFading", &Script_SetFading },
    { "GetFading", &Script_GetFading },
    { "SetTimeVisible", &Script_SetTimeVisible },
    { "GetTimeVisible", &Script_GetTimeVisible },
    { "SetFadeDuration", &Script_SetFadeDuration },
    { "GetFadeDuration", &Script_GetFadeDuration },
    { "Clear", &Script_Clear },
    { "SetInsertMode", &Script_SetInsertMode },
    { "GetInsertMode", &Script_GetInsertMode },
    { "SetIndentedWordWrap", &Script_SetIndentedWordWrap },
    { "GetIndentedWordWrap", &Script_GetIndentedWordWrap },
    { "SetHyperlinksEnabled", &Script_SetHyperlinksEnabled },
    { "GetHyperlinksEnabled", &Script_GetHyperlinksEnabled },
};

// VERIFY: f7ba05fc auto
// OFFSET: 0x00972810
void CSimpleMessageScrollFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, s_ScriptMethods, sizeof(s_ScriptMethods) / sizeof(s_ScriptMethods[0]));
}
