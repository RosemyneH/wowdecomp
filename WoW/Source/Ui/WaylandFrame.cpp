#include "WaylandFrame.h"

#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Frame/CSimpleTexture.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/WaylandEmbed.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/CiRect.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Live {
    int32_t id = 0;
    int32_t frameRef = LUA_NOREF;
    CSimpleTexture* texture = nullptr;
    CTexture* image = nullptr;
    int32_t width = 0;
    int32_t height = 0;
    uint32_t serial = 0;
    bool closing = false;
};

std::vector<Live> g_live;

Live* Find(int32_t id) {
    for (Live& live : g_live) if (live.id == id) return &live;
    return nullptr;
}

void FillTexels(EGxTexCommand command, uint32_t, uint32_t, uint32_t, uint32_t, void* user, uint32_t& stride, const void*& texels) {
    if (command != GxTex_Lock && command != GxTex_Latch) return;
    WaylandPixels pixels;
    if (!WaylandEmbedPixels((int32_t)(intptr_t)user, &pixels) || !pixels.data) return;
    stride = (uint32_t)pixels.stride;
    texels = pixels.data;
}

const char* kFrameLua = R"lua(
local id, title, w, h = ...
local parent = UIParent
if not parent then return end
local frame = CreateFrame("Frame", nil, parent)
frame:SetSize((w or 640) + 16, (h or 400) + 36)
frame:SetPoint("CENTER", parent, "CENTER", 40, 20)
frame:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
frame:SetBackdropColor(0.04, 0.04, 0.05, 0.96)
frame:SetBackdropBorderColor(0.72, 0.55, 0.22, 1)
frame:SetFrameStrata("HIGH")
frame:EnableMouse(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", function(self) self:StartMoving() end)
frame:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
frame:SetScript("OnMouseDown", function() WaylandFrame_Focus(id, 1) end)
local label = frame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
label:SetPoint("TOPLEFT", 28, -9)
label:SetText(title or "Wayland")
local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", 2, 2)
close:SetScript("OnClick", function() WaylandFrame_Close(id) end)
local tex = frame:CreateTexture(nil, "ARTWORK")
tex:SetPoint("TOPLEFT", 8, -28)
tex:SetPoint("BOTTOMRIGHT", -8, 8)
tex:EnableMouse(true)
local function point(button, down)
    local x, y = GetCursorPosition()
    local scale = tex:GetEffectiveScale()
    if not scale or scale == 0 then scale = 1 end
    x, y = x / scale, y / scale
    local left, top = tex:GetLeft(), tex:GetTop()
    local tw, th = tex:GetWidth(), tex:GetHeight()
    if not left or not top or not tw or tw == 0 or not th or th == 0 then return end
    WaylandFrame_Pointer(id, (x - left) / tw, (top - y) / th, button or 0, down or 0)
end
tex:SetScript("OnUpdate", function()
    if tex.IsMouseOver and tex:IsMouseOver() then point(0, 0) end
end)
tex:SetScript("OnMouseDown", function(_, button)
    WaylandFrame_Focus(id, 1)
    local map = { LeftButton = 1, RightButton = 2, MiddleButton = 3 }
    point(map[button] or 1, 1)
end)
tex:SetScript("OnMouseUp", function(_, button)
    local map = { LeftButton = 1, RightButton = 2, MiddleButton = 3 }
    point(map[button] or 1, 0)
end)
return tex
)lua";

int32_t LuaClose(lua_State* L) {
    WaylandFrame_Close((int32_t)luaL_checknumber(L, 1));
    return 0;
}

int32_t LuaFocus(lua_State* L) {
    WaylandEmbedFocus((int32_t)luaL_checknumber(L, 1), (int32_t)luaL_checknumber(L, 2));
    return 0;
}

int32_t LuaPointer(lua_State* L) {
    int32_t id = (int32_t)luaL_checknumber(L, 1);
    double nx = luaL_checknumber(L, 2);
    double ny = luaL_checknumber(L, 3);
    int32_t button = (int32_t)luaL_checknumber(L, 4);
    int32_t down = (int32_t)luaL_checknumber(L, 5);
    WaylandPixels pixels;
    int32_t x = 0;
    int32_t y = 0;
    if (WaylandEmbedPixels(id, &pixels) && pixels.width > 0 && pixels.height > 0) {
        if (nx < 0) nx = 0;
        if (ny < 0) ny = 0;
        if (nx > 1) nx = 1;
        if (ny > 1) ny = 1;
        x = (int32_t)(nx * (pixels.width - 1));
        y = (int32_t)(ny * (pixels.height - 1));
    }
    WaylandEmbedPointer(id, x, y, button, down);
    return 0;
}

int32_t LuaOpen(lua_State* L) {
    const char* spec = luaL_checkstring(L, 1);
    int32_t width = lua_isnumber(L, 2) ? (int32_t)lua_tonumber(L, 2) : 640;
    int32_t height = lua_isnumber(L, 3) ? (int32_t)lua_tonumber(L, 3) : 400;
    char error[256] = {};
    int32_t ref = LUA_NOREF;
    int32_t id = WaylandFrame_Open(spec, width, height, &ref, error, sizeof(error));
    if (!id) {
        lua_pushnil(L);
        lua_pushstring(L, error[0] ? error : "wayland frame failed");
        return 2;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

void EnsureTexture(Live& live, const WaylandPixels& pixels) {
    if (live.image && live.width == pixels.width && live.height == pixels.height) return;
    CGxTexFlags flags;
    flags.m_filter = GxTex_Linear;
    CTexture* image = TextureCreate((uint32_t)pixels.width, (uint32_t)pixels.height, GxTex_Argb8888, GxTex_Argb8888, flags, (void*)(intptr_t)live.id, FillTexels, "WaylandFrame", 0);
    if (!image) return;
    if (live.texture) live.texture->SetTexture(image);
    if (live.image) HandleClose(reinterpret_cast<HOBJECT>(live.image));
    live.image = image;
    live.width = pixels.width;
    live.height = pixels.height;
    live.serial = 0;
}

}  // namespace

void WaylandFrame_Register(lua_State* L) {
    if (!L) return;
    lua_pushcfunction(L, LuaOpen);
    lua_setfield(L, LUA_GLOBALSINDEX, "WaylandFrame_Open");
    lua_pushcfunction(L, LuaClose);
    lua_setfield(L, LUA_GLOBALSINDEX, "WaylandFrame_Close");
    lua_pushcfunction(L, LuaFocus);
    lua_setfield(L, LUA_GLOBALSINDEX, "WaylandFrame_Focus");
    lua_pushcfunction(L, LuaPointer);
    lua_setfield(L, LUA_GLOBALSINDEX, "WaylandFrame_Pointer");
}

int32_t WaylandFrame_Open(const char* spec, int32_t width, int32_t height, int32_t* frameRef, char* error, uint32_t errorLen) {
    if (!WaylandEmbedAvailable()) {
        if (error && errorLen) std::snprintf(error, errorLen, "wayland frames are a Linux host");
        return 0;
    }
    lua_State* L = FrameScript_GetContext();
    if (!L) {
        if (error && errorLen) std::snprintf(error, errorLen, "ui is not ready");
        return 0;
    }
    char launchError[256] = {};
    int32_t id = WaylandEmbedOpen(spec, width, height, launchError, sizeof(launchError));
    if (!id) {
        if (error && errorLen) std::snprintf(error, errorLen, "%s", launchError[0] ? launchError : "wayland open failed");
        return 0;
    }
    if (luaL_loadbuffer(L, kFrameLua, std::strlen(kFrameLua), "WaylandFrame") != 0) {
        WaylandEmbedClose(id);
        if (error && errorLen) std::snprintf(error, errorLen, "frame script failed");
        return 0;
    }
    lua_pushnumber(L, id);
    lua_pushstring(L, WaylandEmbedTitle(id));
    lua_pushnumber(L, width);
    lua_pushnumber(L, height);
    if (lua_pcall(L, 4, 1, 0) != 0) {
        WaylandEmbedClose(id);
        lua_pop(L, 1);
        if (error && errorLen) std::snprintf(error, errorLen, "could not build the frame");
        return 0;
    }
    CSimpleTexture* texture = lua_isuserdata(L, -1) ? static_cast<CSimpleTexture*>(lua_touserdata(L, -1)) : nullptr;
    int top = lua_gettop(L);
    lua_getfield(L, top, "GetParent");
    int32_t ref = LUA_NOREF;
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, top);
        if (lua_pcall(L, 1, 1, 0) == 0) {
            lua_pushvalue(L, -1);
            ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }
    lua_settop(L, top - 1);
    Live live;
    live.id = id;
    live.frameRef = ref;
    live.texture = texture;
    g_live.push_back(live);
    if (frameRef) {
        if (ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            *frameRef = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            *frameRef = LUA_NOREF;
        }
    }
    return id;
}

void HideFrame(Live& live) {
    lua_State* L = FrameScript_GetContext();
    if (!L || live.frameRef == LUA_NOREF) return;
    lua_rawgeti(L, LUA_REGISTRYINDEX, live.frameRef);
    if (lua_isuserdata(L, -1)) {
        lua_getfield(L, -1, "Hide");
        if (lua_isfunction(L, -1)) {
            lua_pushvalue(L, -2);
            lua_pcall(L, 1, 0, 0);
        }
    }
    lua_pop(L, 1);
}

void ReleaseLive(Live& live) {
    lua_State* L = FrameScript_GetContext();
    if (L && live.frameRef != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, live.frameRef);
    if (live.image) HandleClose(reinterpret_cast<HOBJECT>(live.image));
    live.frameRef = LUA_NOREF;
    live.image = nullptr;
}

void WaylandFrame_Close(int32_t id) {
    WaylandEmbedClose(id);
    Live* live = Find(id);
    if (!live || live->closing) return;
    live->closing = true;
    HideFrame(*live);
}

void WaylandFrame_Present() {
    WaylandEmbedService();
    for (auto it = g_live.begin(); it != g_live.end();) {
        if (it->closing) {
            ReleaseLive(*it);
            it = g_live.erase(it);
            continue;
        }
        ++it;
    }
    for (Live& live : g_live) {
        WaylandPixels pixels;
        if (!WaylandEmbedPixels(live.id, &pixels) || !pixels.data || pixels.width < 1 || pixels.height < 1) continue;
        if (pixels.serial == live.serial && live.image) continue;
        EnsureTexture(live, pixels);
        if (!live.image) continue;
        CGxTex* gx = TextureGetGxTex(live.image, 0, nullptr);
        if (!gx) continue;
        CiRect rect;
        rect.minY = 0;
        rect.minX = 0;
        rect.maxY = pixels.height;
        rect.maxX = pixels.width;
        GxTexUpdate(gx, rect, 1);
        live.serial = pixels.serial;
    }
}
