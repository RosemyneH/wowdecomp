#include "WoW/Source/WasmAddonClient.h"
#include "Engine/Source/Base/Status.h"
#include "WoW/Source/Ui/AddOns.h"
#include "WoW/Source/Ui/AuctionHouse.h"
#include "WoW/Source/Ui/ChatFrame.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/Ui/WasmConfirmUi.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include "Engine/Source/ConsoleBase/ConsoleCommand.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Storm/Source/MD5.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameXML/FrameXML.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/WasmAddon/WasmAddonManager.h"
#include "Engine/Source/WasmAddon/WasmConfirm.h"
#include "Engine/Source/WasmAddon/WasmManifest.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

bool LoadAddOn(const char* name, bool demandLoad, CStatus* status);

namespace {

namespace fs = std::filesystem;

struct TaintScope {
    const char* saved;
    explicit TaintScope(const std::string& addonId) : saved(lua_tainted) {
        m_taint = "WasmAddon:" + addonId;
        lua_tainted = m_taint.c_str();
    }
    ~TaintScope() { lua_tainted = saved; }
    std::string m_taint;
};

// ʕ •ᴥ•ʔ✿ Game-facing services: every call goes through the existing Lua/UI layer on the main thread ✿ ʕ •ᴥ•ʔ
class ClientHostServices final : public WasmAddon::IWasmHostServices {
public:
    void DropUiReferences() { m_ui.clear(); }

    // ʕ ●ᴥ●ʔ✿ All Lua work runs under lua_cpcall so a replaced global or erroring __index cannot longjmp through Wasmtime ✿ ʕ ●ᴥ●ʔ
    bool ChatPrint(const std::string& addonId, const std::string& text) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, &addonId, &text, 0, nullptr};
        return lua_cpcall(L, &TrampChat, &ctx) == 0 && ctx.ok;
    }

    bool GetInventorySnapshot(std::vector<WasmAddon::InventoryItem>& out) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, nullptr, 0, &out};
        return lua_cpcall(L, &TrampInventory, &ctx) == 0 && ctx.ok;
    }

    bool GetUnitSnapshot(const std::vector<std::string>& tokens, std::vector<WasmAddon::UnitRecord>& out) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, nullptr, 0, nullptr};
        ctx.tokens = &tokens;
        ctx.units = &out;
        return lua_cpcall(L, &TrampUnits, &ctx) == 0 && ctx.ok;
    }

    bool GetAuctionSnapshot(int32_t kind, WasmAddon::AuctionSnapshot& out) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, nullptr, (uint64_t)(int64_t)kind, nullptr};
        ctx.auction = &out;
        return lua_cpcall(L, &TrampAuction, &ctx) == 0 && ctx.ok;
    }

    uint64_t UiCreate(const std::string& addonId, const std::string& kind) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return 0;
        HostCallCtx ctx{this, &addonId, &kind, 0, nullptr};
        return lua_cpcall(L, &TrampUiCreate, &ctx) == 0 ? ctx.result : 0;
    }

    bool UiSetText(uint64_t handle, const std::string& text) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, &text, handle, nullptr};
        return lua_cpcall(L, &TrampUiSetText, &ctx) == 0 && ctx.ok;
    }

    uint64_t UiSpawn(const std::string& addonId, uint64_t parentHost, const std::string& kind) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return 0;
        HostCallCtx ctx{this, &addonId, &kind, 0, nullptr};
        ctx.parent = parentHost;
        return lua_cpcall(L, &TrampUiSpawn, &ctx) == 0 ? ctx.result : 0;
    }

    bool UiStyle(uint64_t handle, int32_t op, const uint8_t* data, uint32_t len) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, nullptr, handle, nullptr};
        ctx.op = op;
        ctx.bytes = data;
        ctx.byteLen = len;
        return lua_cpcall(L, &TrampUiStyle, &ctx) == 0 && ctx.ok;
    }

    bool UiBind(uint64_t handle, int32_t guestHandle, const std::string& event) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, &event, handle, nullptr};
        ctx.guest = guestHandle;
        return lua_cpcall(L, &TrampUiBind, &ctx) == 0 && ctx.ok;
    }

    bool UiGetText(uint64_t handle, std::string& out) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, nullptr, handle, nullptr};
        ctx.textOut = &out;
        return lua_cpcall(L, &TrampUiGetText, &ctx) == 0 && ctx.ok;
    }

    bool GetUnitNames(const std::vector<std::string>& tokens, std::vector<std::pair<uint32_t, std::string>>& out) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) return false;
        HostCallCtx ctx{this, nullptr, nullptr, 0, nullptr};
        ctx.tokens = &tokens;
        ctx.names = &out;
        return lua_cpcall(L, &TrampUnitNames, &ctx) == 0 && ctx.ok;
    }

    int ExecuteLua(const std::string& addonId, const std::string& chunkName, const std::string& source, std::string& error) override {
        return RunLua(addonId, chunkName, source.data(), source.size(), error);
    }

    int LoadLuaPath(const std::string& addonId, const std::string& addonRoot, const std::string& absolutePath, std::string& error) override {
        std::set<std::string> seen;
        return LoadLuaFile(addonId, addonRoot, absolutePath, error, seen, 0);
    }

    int LoadNamedAddon(const std::string& name, std::string& error) override {
        lua_State* L = FrameScript_GetContext();
        if (!L) { error = "lua state unavailable"; return WasmAddon::ErrUnavailable; }
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "IsAddOnLoaded");
        if (lua_isfunction(L, -1)) {
            lua_pushstring(L, name.c_str());
            if (lua_pcall(L, 1, 1, 0) == 0 && lua_toboolean(L, -1)) {
                lua_settop(L, top);
                return WasmAddon::Ok;
            }
        }
        lua_settop(L, top);
        CStatus status;
        if (!LoadAddOn(name.c_str(), true, &status)) {
            error = "addon not found or not loadable: " + name;
            return WasmAddon::ErrNotFound;
        }
        return WasmAddon::Ok;
    }

    void UiDestroy(uint64_t handle) override {
        auto it = m_ui.find(handle);
        lua_State* L = FrameScript_GetContext();
        if (it == m_ui.end() || !L) return;
        if (L != m_seenState) { m_ui.erase(it); return; }
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.mainRef);
        if (lua_isuserdata(L, -1) || lua_istable(L, -1)) {
            lua_getfield(L, -1, "Hide");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                lua_pcall(L, 1, 0, 0);
            }
        }
        lua_settop(L, top);
        luaL_unref(L, LUA_REGISTRYINDEX, it->second.mainRef);
        if (it->second.textRef != it->second.mainRef && it->second.textRef != LUA_NOREF)
            luaL_unref(L, LUA_REGISTRYINDEX, it->second.textRef);
        m_ui.erase(it);
    }

    WasmAddon::ConfirmExecResult ExecuteChatSend(const WasmAddon::ConfirmRequest& request) override {
        int32_t rc = CGChat::SendNative(request.chatType, 0xFFFFFFFEu, request.target.c_str(), request.message.c_str());
        return {rc == 0, rc};
    }

    WasmAddon::ConfirmExecResult ExecuteAuctionBid(const WasmAddon::ConfirmRequest& request) override {
        if (request.auctionIndex < 1) return {false, 1};
        int32_t rc = CGAuctionHouse::PlaceBidNative(request.listKind, request.auctionIndex - 1, request.bid);
        return {rc == 0, rc};
    }

    void ConfirmShow(const WasmAddon::ConfirmRequest& request) override;
    void ConfirmHide(uint32_t requestId) override;

    bool ResolveAuctionIdentity(int32_t listKind, uint32_t index, WasmAddon::AuctionIdentity& out) override {
        if (index < 1) return false;
        AuctionItemInfo* auction = nullptr;
        uint32_t slot = index - 1;
        if (listKind == 0) auction = GetListAuction(slot);
        else if (listKind == 1) auction = GetOwnerAuction(slot);
        else if (listKind == 2) auction = GetBidderAuction(slot);
        if (!auction) return false;
        out.auctionId = auction->m_auctionID;
        out.itemId = auction->m_itemID;
        out.count = auction->m_count;
        out.buyout = auction->m_buyout;
        out.minBid = auction->m_minBid;
        return true;
    }

    void Log(const std::string& message) override {
        ConsoleWrite(message.c_str(), DEFAULT_COLOR);
        // ʕ ◕ᴥ◕ ʔ✿ Mirror to stdout: the in-game console has no file sink, headless runs need this ✿ ʕ ◕ᴥ◕ ʔ
        if (!ConsoleLogTeeEnabled()) {
            fprintf(stdout, "%s\n", message.c_str());
            fflush(stdout);
        }
    }

private:
    struct HostCallCtx {
        ClientHostServices* self;
        const std::string* s1;
        const std::string* s2;
        uint64_t handle;
        std::vector<WasmAddon::InventoryItem>* items;
        const std::vector<std::string>* tokens = nullptr;
        std::vector<WasmAddon::UnitRecord>* units = nullptr;
        WasmAddon::AuctionSnapshot* auction = nullptr;
        bool ok = false;
        uint64_t result = 0;
        uint64_t parent = 0;
        int32_t op = 0;
        const uint8_t* bytes = nullptr;
        uint32_t byteLen = 0;
        std::vector<std::pair<uint32_t, std::string>>* names = nullptr;
        std::string* textOut = nullptr;
        int32_t guest = 0;
    };
    static int TrampChat(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoChatPrint(L, *c->s1, *c->s2); return 0; }
    static int TrampInventory(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoInventory(L, *c->items); return 0; }
    static int TrampUiCreate(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->result = c->self->DoUiCreate(L, *c->s1, *c->s2); return 0; }
    static int TrampUiSpawn(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->result = c->self->DoUiSpawn(L, *c->s1, c->parent, *c->s2); return 0; }
    static int TrampUiStyle(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoUiStyle(L, c->handle, c->op, c->bytes, c->byteLen); return 0; }
    static int TrampUiSetText(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoUiSetText(L, c->handle, *c->s2); return 0; }
    static int TrampUnits(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoUnits(L, *c->tokens, *c->units); return 0; }
    static int TrampAuction(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoAuction(L, (int32_t)c->handle, *c->auction); return 0; }
    static int TrampUiBind(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoUiBind(L, c->handle, c->guest, *c->s2); return 0; }
    static int TrampUiGetText(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoUiGetText(L, c->handle, *c->textOut); return 0; }
    static int TrampUnitNames(lua_State* L) { auto* c = (HostCallCtx*)lua_touserdata(L, 1); c->ok = c->self->DoUnitNames(L, *c->tokens, *c->names); return 0; }

    static double UnitCall(lua_State* L, const char* fn, const std::string& token, int32_t retIndex, bool* isBool = nullptr) {
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, fn);
        lua_pushstring(L, token.c_str());
        double result = 0;
        if (lua_pcall(L, 1, LUA_MULTRET, 0) == 0 && lua_gettop(L) >= top + retIndex) {
            if (isBool) *isBool = lua_toboolean(L, top + retIndex) != 0;
            else result = lua_tonumber(L, top + retIndex);
        }
        lua_settop(L, top);
        return result;
    }

    bool DoUnits(lua_State* L, const std::vector<std::string>& tokens, std::vector<WasmAddon::UnitRecord>& out) {
        TaintScope taint("units");
        for (size_t i = 0; i < tokens.size(); i++) {
            WasmAddon::UnitRecord record;
            record.tokenIndex = (uint32_t)i;
            bool exists = false;
            UnitCall(L, "UnitExists", tokens[i], 1, &exists);
            if (exists) {
                bool dead = false, ghost = false, isPlayer = false;
                UnitCall(L, "UnitIsDead", tokens[i], 1, &dead);
                UnitCall(L, "UnitIsGhost", tokens[i], 1, &ghost);
                UnitCall(L, "UnitIsPlayer", tokens[i], 1, &isPlayer);
                record.flags = 1 | ((dead || ghost) ? 2 : 0) | (isPlayer ? 4 : 0) | (tokens[i] == "player" ? 8 : 0);
                record.health = (uint32_t)UnitCall(L, "UnitHealth", tokens[i], 1);
                record.healthMax = (uint32_t)UnitCall(L, "UnitHealthMax", tokens[i], 1);
                record.power = (uint32_t)UnitCall(L, "UnitPower", tokens[i], 1);
                record.powerMax = (uint32_t)UnitCall(L, "UnitPowerMax", tokens[i], 1);
                record.powerType = (uint32_t)UnitCall(L, "UnitPowerType", tokens[i], 1);
                record.level = (uint32_t)UnitCall(L, "UnitLevel", tokens[i], 1);
                record.classId = (uint32_t)UnitCall(L, "UnitClass", tokens[i], 3);
                record.raceId = (uint32_t)UnitCall(L, "UnitRace", tokens[i], 3);
            }
            out.push_back(record);
        }
        return true;
    }

    // ʕ •ᴥ•ʔ✿ UnitName's later returns are realm; only a string name leaves the host ✿ ʕ •ᴥ•ʔ
    static std::string UnitNameOf(lua_State* L, const std::string& token) {
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "UnitName");
        lua_pushstring(L, token.c_str());
        std::string name;
        if (lua_pcall(L, 1, LUA_MULTRET, 0) == 0 && lua_type(L, top + 1) == LUA_TSTRING) {
            size_t len = 0;
            const char* text = lua_tolstring(L, top + 1, &len);
            if (text && len) {
                char buf[64];
                size_t n = len < sizeof(buf) ? len : sizeof(buf);
                std::memcpy(buf, text, n);
                name = WasmAddon::SanitizeDisplayText(std::string(buf, buf + n));
            }
        }
        lua_settop(L, top);
        return name;
    }

    bool DoUnitNames(lua_State* L, const std::vector<std::string>& tokens, std::vector<std::pair<uint32_t, std::string>>& out) {
        TaintScope taint("units");
        out.clear();
        for (size_t i = 0; i < tokens.size(); i++)
            out.emplace_back((uint32_t)i, UnitNameOf(L, tokens[i]));
        return true;
    }

    bool DoAuction(lua_State* L, int32_t kind, WasmAddon::AuctionSnapshot& out) {
        TaintScope taint("auction");
        static const char* kKinds[] = {"list", "owner", "bidder"};
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "UnitName");
        lua_pushstring(L, "player");
        std::string playerName = lua_pcall(L, 1, 1, 0) == 0 && lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_settop(L, top);
        lua_getfield(L, LUA_GLOBALSINDEX, "GetNumAuctionItems");
        lua_pushstring(L, kKinds[kind]);
        if (lua_pcall(L, 1, 2, 0) != 0) { lua_settop(L, top); return false; }
        int32_t num = (int32_t)lua_tonumber(L, -2);
        out.total = (uint32_t)lua_tonumber(L, -1);
        out.flags = 1;
        lua_settop(L, top);
        for (int32_t i = 1; i <= num && i <= 200; i++) {
            lua_getfield(L, LUA_GLOBALSINDEX, "GetAuctionItemInfo");
            lua_pushstring(L, kKinds[kind]);
            lua_pushnumber(L, i);
            if (lua_pcall(L, 2, 13, 0) != 0) { lua_settop(L, top); continue; }
            int base = lua_gettop(L) - 12;
            WasmAddon::AuctionRecord record;
            record.index = (uint32_t)i;
            record.count = (uint32_t)lua_tonumber(L, base + 2);
            record.quality = (uint32_t)lua_tonumber(L, base + 3);
            record.minBid = (uint32_t)lua_tonumber(L, base + 6);
            record.minIncrement = (uint32_t)lua_tonumber(L, base + 7);
            record.buyout = (uint32_t)lua_tonumber(L, base + 8);
            record.bid = (uint32_t)lua_tonumber(L, base + 9);
            bool highBidder = lua_isnumber(L, base + 10) && lua_tonumber(L, base + 10) == 1.0;
            std::string owner = lua_isstring(L, base + 11) ? lua_tostring(L, base + 11) : "";
            bool sold = lua_tonumber(L, base + 12) != 0;
            record.flags = (highBidder ? 1 : 0) | (!playerName.empty() && owner == playerName ? 2 : 0) | (sold ? 4 : 0);
            lua_settop(L, top);
            // ʕ ◕ᴥ◕ ʔ✿ Item identity comes from parsing our own link format, never from guest input ✿ ʕ ◕ᴥ◕ ʔ
            lua_getfield(L, LUA_GLOBALSINDEX, "GetAuctionItemLink");
            lua_pushstring(L, kKinds[kind]);
            lua_pushnumber(L, i);
            if (lua_pcall(L, 2, 1, 0) == 0 && lua_isstring(L, -1)) {
                const char* link = lua_tostring(L, -1);
                const char* item = link ? std::strstr(link, "item:") : nullptr;
                if (item) record.itemId = (uint32_t)std::strtoul(item + 5, nullptr, 10);
            }
            lua_settop(L, top);
            lua_getfield(L, LUA_GLOBALSINDEX, "GetAuctionItemTimeLeft");
            lua_pushstring(L, kKinds[kind]);
            lua_pushnumber(L, i);
            if (lua_pcall(L, 2, 1, 0) == 0) record.timeLeft = (uint32_t)lua_tonumber(L, -1);
            lua_settop(L, top);
            out.records.push_back(record);
        }
        return true;
    }

    bool DoChatPrint(lua_State* L, const std::string& addonId, const std::string& text) {
        TaintScope taint(addonId);
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "DEFAULT_CHAT_FRAME");
        if (lua_isnil(L, -1)) { lua_settop(L, top); return false; }
        lua_getfield(L, -1, "AddMessage");
        lua_pushvalue(L, -2);
        lua_pushstring(L, ("[" + addonId + "] " + text).c_str());
        bool ok = lua_pcall(L, 2, 0, 0) == 0;
        lua_settop(L, top);
        return ok;
    }

    bool DoInventory(lua_State* L, std::vector<WasmAddon::InventoryItem>& out) {
        TaintScope taint("inventory");
        int top = lua_gettop(L);
        for (int32_t bag = 0; bag <= 4; bag++) {
            int32_t slots = CallInt(L, "GetContainerNumSlots", bag);
            for (int32_t slot = 1; slot <= slots; slot++) {
                int32_t itemId = CallInt2(L, "GetContainerItemID", bag, slot);
                if (!itemId) continue;
                int32_t count = CallSecondInt(L, "GetContainerItemInfo", bag, slot);
                out.push_back({(uint32_t)bag, (uint32_t)slot, (uint32_t)itemId, (uint32_t)(count > 0 ? count : 1)});
            }
        }
        lua_settop(L, top);
        return true;
    }

    uint64_t DoUiCreate(lua_State* L, const std::string& addonId, const std::string& kind) {
        TaintScope taint(addonId);
        TrackState(L);
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "UIParent");
        if (lua_isnil(L, -1)) { lua_settop(L, top); return 0; }
        int32_t textRef = LUA_NOREF;
        if (kind == "frame") {
            lua_getfield(L, LUA_GLOBALSINDEX, "CreateFrame");
            lua_pushstring(L, "Frame");
            lua_pushnil(L);
            lua_pushvalue(L, top + 1);
            if (lua_pcall(L, 4, 1, 0) != 0) { lua_settop(L, top); return 0; }
            lua_getfield(L, -1, "SetSize");
            lua_pushvalue(L, -2); lua_pushnumber(L, 200); lua_pushnumber(L, 60);
            lua_pcall(L, 3, 0, 0);
            lua_getfield(L, -1, "SetPoint");
            lua_pushvalue(L, -2); lua_pushstring(L, "CENTER");
            lua_pcall(L, 2, 0, 0);
            textRef = MakeFontString(L);
            if (textRef == LUA_NOREF) { lua_settop(L, top); return 0; }
            int32_t frameRef = luaL_ref(L, LUA_REGISTRYINDEX);
            uint64_t handle = m_nextHandle++;
            m_ui[handle] = {frameRef, textRef, WidgetKind::Frame};
            lua_settop(L, top);
            return handle;
        }
        textRef = MakeFontString(L);
        lua_settop(L, top);
        if (textRef == LUA_NOREF) return 0;
        uint64_t handle = m_nextHandle++;
        m_ui[handle] = {textRef, textRef, WidgetKind::FontString};
        return handle;
    }

    bool PushParent(lua_State* L, uint64_t parentHost) {
        if (parentHost == 0) {
            lua_getfield(L, LUA_GLOBALSINDEX, "UIParent");
            return !lua_isnil(L, -1);
        }
        auto it = m_ui.find(parentHost);
        if (it == m_ui.end() || it->second.mainRef == LUA_NOREF) return false;
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.mainRef);
        return lua_istable(L, -1) || lua_isuserdata(L, -1);
    }

    template <class Push>
    bool CallOn(lua_State* L, int32_t ref, const char* method, int extra, Push push) {
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) { lua_settop(L, top); return false; }
        lua_getfield(L, -1, method);
        if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
        lua_pushvalue(L, -2);
        push(L);
        bool ok = lua_pcall(L, 1 + extra, 0, 0) == 0;
        lua_settop(L, top);
        return ok;
    }

    static int32_t ReadI32(const uint8_t* p) {
        return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
    }

    static int32_t MakeChildFont(lua_State* L) {
        int parent = lua_gettop(L);
        lua_getfield(L, parent, "CreateFontString");
        if (!lua_isfunction(L, -1)) return LUA_NOREF;
        lua_pushvalue(L, parent);
        lua_pushnil(L);
        lua_pushstring(L, "OVERLAY");
        lua_pushstring(L, "GameFontHighlightSmall");
        if (lua_pcall(L, 4, 1, 0) != 0) return LUA_NOREF;
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }

    static int32_t MakeChildTexture(lua_State* L) {
        int parent = lua_gettop(L);
        lua_getfield(L, parent, "CreateTexture");
        if (!lua_isfunction(L, -1)) return LUA_NOREF;
        lua_pushvalue(L, parent);
        lua_pushnil(L);
        lua_pushstring(L, "ARTWORK");
        if (lua_pcall(L, 3, 1, 0) != 0) return LUA_NOREF;
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }

    static bool MakeChildFrame(lua_State* L, const char* type, bool statusBar, bool editBox, int32_t& ref) {
        int parent = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, "CreateFrame");
        lua_pushstring(L, type);
        lua_pushnil(L);
        lua_pushvalue(L, parent);
        if (lua_pcall(L, 3, 1, 0) != 0) return false;
        if (editBox) {
            lua_getfield(L, -1, "SetFont");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                lua_pushstring(L, "Fonts\\FRIZQT__.TTF");
                lua_pushnumber(L, 12);
                if (lua_pcall(L, 3, 0, 0) != 0) lua_pop(L, 1);
            } else {
                lua_pop(L, 1);
            }
            lua_getfield(L, -1, "SetAutoFocus");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                lua_pushboolean(L, 0);
                if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1);
            } else {
                lua_pop(L, 1);
            }
            lua_getfield(L, -1, "SetTextInsets");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, 0);
                lua_pushnumber(L, 0);
                if (lua_pcall(L, 5, 0, 0) != 0) lua_pop(L, 1);
            } else {
                lua_pop(L, 1);
            }
        }
        if (statusBar) {
            lua_getfield(L, -1, "SetStatusBarTexture");
            lua_pushvalue(L, -2);
            lua_pushstring(L, "Interface\\TargetingFrame\\UI-StatusBar");
            lua_pcall(L, 2, 0, 0);
            lua_getfield(L, -1, "SetMinMaxValues");
            lua_pushvalue(L, -2);
            lua_pushnumber(L, 0);
            lua_pushnumber(L, 1);
            lua_pcall(L, 3, 0, 0);
        }
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
        return ref != LUA_NOREF;
    }

    uint64_t DoUiSpawn(lua_State* L, const std::string& addonId, uint64_t parentHost, const std::string& kind) {
        TaintScope taint(addonId);
        TrackState(L);
        int top = lua_gettop(L);
        if (!PushParent(L, parentHost)) { lua_settop(L, top); return 0; }
        int32_t ref = LUA_NOREF;
        WidgetKind wk = WidgetKind::Frame;
        bool ok = false;
        if (kind == "fontstring") {
            ref = MakeChildFont(L);
            ok = ref != LUA_NOREF;
            wk = WidgetKind::FontString;
        } else if (kind == "texture") {
            ref = MakeChildTexture(L);
            ok = ref != LUA_NOREF;
            wk = WidgetKind::Texture;
        } else if (kind == "frame" || kind == "statusbar" || kind == "button" || kind == "editbox" || kind == "scrollframe") {
            const char* type = "Frame";
            if (kind == "statusbar") type = "StatusBar";
            else if (kind == "button") type = "Button";
            else if (kind == "editbox") type = "EditBox";
            else if (kind == "scrollframe") type = "ScrollFrame";
            ok = MakeChildFrame(L, type, kind == "statusbar", kind == "editbox", ref);
            if (kind == "statusbar") wk = WidgetKind::StatusBar;
            else if (kind == "button") wk = WidgetKind::Button;
            else if (kind == "editbox") wk = WidgetKind::EditBox;
            else if (kind == "scrollframe") wk = WidgetKind::ScrollFrame;
            else wk = WidgetKind::Frame;
        }
        lua_settop(L, top);
        if (!ok || ref == LUA_NOREF) {
            if (ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, ref);
            return 0;
        }
        uint64_t handle = m_nextHandle++;
        int32_t textRef = (wk == WidgetKind::FontString || wk == WidgetKind::EditBox) ? ref : LUA_NOREF;
        m_ui[handle] = {ref, textRef, wk};
        return handle;
    }

    bool ApplyBackdrop(lua_State* L, int32_t ref, const uint8_t* rgba) {
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) { lua_settop(L, top); return false; }
        lua_getfield(L, -1, "SetBackdrop");
        if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
        lua_pushvalue(L, -2);
        lua_newtable(L);
        auto putStr = [&](const char* key, const char* value) {
            lua_pushstring(L, key);
            lua_pushstring(L, value);
            lua_settable(L, -3);
        };
        auto putNum = [&](const char* key, double value) {
            lua_pushstring(L, key);
            lua_pushnumber(L, value);
            lua_settable(L, -3);
        };
        putStr("bgFile", "Interface\\ChatFrame\\ChatFrameBackground");
        putStr("edgeFile", "Interface\\Tooltips\\UI-Tooltip-Border");
        lua_pushstring(L, "tile");
        lua_pushboolean(L, 1);
        lua_settable(L, -3);
        putNum("tileSize", 16);
        putNum("edgeSize", 12);
        lua_pushstring(L, "insets");
        lua_newtable(L);
        putNum("left", 3);
        putNum("right", 3);
        putNum("top", 3);
        putNum("bottom", 3);
        lua_settable(L, -3);
        if (lua_pcall(L, 2, 0, 0) != 0) { lua_settop(L, top); return false; }
        lua_settop(L, top);
        auto pushBg = [&](lua_State* S) {
            lua_pushnumber(S, rgba[0] / 255.0);
            lua_pushnumber(S, rgba[1] / 255.0);
            lua_pushnumber(S, rgba[2] / 255.0);
            lua_pushnumber(S, rgba[3] / 255.0);
        };
        if (!CallOn(L, ref, "SetBackdropColor", 4, pushBg)) return false;
        CallOn(L, ref, "SetBackdropBorderColor", 4, [](lua_State* S) {
            lua_pushnumber(S, 0);
            lua_pushnumber(S, 0);
            lua_pushnumber(S, 0);
            lua_pushnumber(S, 1);
        });
        return true;
    }

    bool DoUiStyle(lua_State* L, uint64_t handle, int32_t op, const uint8_t* data, uint32_t len) {
        auto it = m_ui.find(handle);
        if (it == m_ui.end() || !data) return false;
        TaintScope taint("ui");
        TrackState(L);
        it = m_ui.find(handle);
        if (it == m_ui.end()) return false;
        const UiRefs& widget = it->second;
        auto pushColor = [&](lua_State* S) {
            lua_pushnumber(S, data[0] / 255.0);
            lua_pushnumber(S, data[1] / 255.0);
            lua_pushnumber(S, data[2] / 255.0);
            lua_pushnumber(S, data[3] / 255.0);
        };
        switch ((WasmAddon::UiOp)op) {
            case WasmAddon::UiOp::SetSize:
                if (len < 8) return false;
                return CallOn(L, widget.mainRef, "SetWidth", 1, [&](lua_State* S) { lua_pushnumber(S, ReadI32(data)); })
                    && CallOn(L, widget.mainRef, "SetHeight", 1, [&](lua_State* S) { lua_pushnumber(S, ReadI32(data + 4)); });
            case WasmAddon::UiOp::SetPoint: {
                if (len < 16) return false;
                static const char* kPoints[] = {"TOPLEFT", "TOP", "TOPRIGHT", "LEFT", "CENTER", "RIGHT", "BOTTOMLEFT", "BOTTOM", "BOTTOMRIGHT"};
                if (data[0] > 8 || data[1] > 8) return false;
                int32_t relHost = ReadI32(data + 4);
                int top = lua_gettop(L);
                lua_rawgeti(L, LUA_REGISTRYINDEX, widget.mainRef);
                if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) { lua_settop(L, top); return false; }
                int self = lua_gettop(L);
                if (relHost == 0) {
                    lua_getfield(L, self, "GetParent");
                    lua_pushvalue(L, self);
                    if (lua_pcall(L, 1, 1, 0) != 0 || lua_isnil(L, -1)) {
                        lua_settop(L, self);
                        lua_getfield(L, LUA_GLOBALSINDEX, "UIParent");
                    }
                } else {
                    auto rel = m_ui.find((uint64_t)relHost);
                    if (rel == m_ui.end()) { lua_settop(L, top); return false; }
                    lua_rawgeti(L, LUA_REGISTRYINDEX, rel->second.mainRef);
                }
                int relative = lua_gettop(L);
                lua_getfield(L, self, "ClearAllPoints");
                if (lua_isfunction(L, -1)) {
                    lua_pushvalue(L, self);
                    lua_pcall(L, 1, 0, 0);
                } else {
                    lua_pop(L, 1);
                }
                lua_getfield(L, self, "SetPoint");
                if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
                lua_pushvalue(L, self);
                lua_pushstring(L, kPoints[data[0]]);
                lua_pushvalue(L, relative);
                lua_pushstring(L, kPoints[data[1]]);
                lua_pushnumber(L, ReadI32(data + 8));
                lua_pushnumber(L, ReadI32(data + 12));
                bool ok = lua_pcall(L, 6, 0, 0) == 0;
                lua_settop(L, top);
                return ok;
            }
            case WasmAddon::UiOp::SetColor:
                if (len < 4) return false;
                if (widget.kind == WidgetKind::StatusBar) return CallOn(L, widget.mainRef, "SetStatusBarColor", 4, pushColor);
                if (widget.kind == WidgetKind::FontString) return CallOn(L, widget.mainRef, "SetTextColor", 4, pushColor);
                if (widget.kind == WidgetKind::Texture) return CallOn(L, widget.mainRef, "SetVertexColor", 4, pushColor);
                return CallOn(L, widget.mainRef, "SetBackdropColor", 4, pushColor);
            case WasmAddon::UiOp::SetShown:
                if (len < 1) return false;
                return CallOn(L, widget.mainRef, data[0] ? "Show" : "Hide", 0, [](lua_State*) {});
            case WasmAddon::UiOp::SetValue:
                if (len < 12 || widget.kind != WidgetKind::StatusBar) return false;
                return CallOn(L, widget.mainRef, "SetMinMaxValues", 2, [&](lua_State* S) {
                           lua_pushnumber(S, ReadI32(data + 4));
                           lua_pushnumber(S, ReadI32(data + 8));
                       })
                    && CallOn(L, widget.mainRef, "SetValue", 1, [&](lua_State* S) { lua_pushnumber(S, ReadI32(data)); });
            case WasmAddon::UiOp::SetJustify: {
                if (len < 2 || widget.kind != WidgetKind::FontString || data[0] > 2 || data[1] > 2) return false;
                static const char* kH[] = {"LEFT", "CENTER", "RIGHT"};
                static const char* kV[] = {"TOP", "MIDDLE", "BOTTOM"};
                return CallOn(L, widget.mainRef, "SetJustifyH", 1, [&](lua_State* S) { lua_pushstring(S, kH[data[0]]); })
                    && CallOn(L, widget.mainRef, "SetJustifyV", 1, [&](lua_State* S) { lua_pushstring(S, kV[data[1]]); });
            }
            case WasmAddon::UiOp::SetFontSize:
                if (len < 4 || widget.kind != WidgetKind::FontString) return false;
                return CallOn(L, widget.mainRef, "SetFont", 2, [&](lua_State* S) {
                    lua_pushstring(S, "Fonts\\FRIZQT__.TTF");
                    lua_pushnumber(S, ReadI32(data));
                });
            case WasmAddon::UiOp::SetBackdrop:
                if (len < 4 || (widget.kind != WidgetKind::Frame && widget.kind != WidgetKind::Button && widget.kind != WidgetKind::StatusBar)) return false;
                return ApplyBackdrop(L, widget.mainRef, data);
            case WasmAddon::UiOp::SetTextColor:
                if (len < 4 || widget.kind != WidgetKind::FontString) return false;
                return CallOn(L, widget.mainRef, "SetTextColor", 4, pushColor);
            case WasmAddon::UiOp::SetLevel:
                if (len < 4 || widget.kind == WidgetKind::FontString || widget.kind == WidgetKind::Texture) return false;
                return CallOn(L, widget.mainRef, "SetFrameLevel", 1, [&](lua_State* S) { lua_pushnumber(S, ReadI32(data)); });
            case WasmAddon::UiOp::SetStrata: {
                if (len < 1 || data[0] > 7 || widget.kind == WidgetKind::FontString || widget.kind == WidgetKind::Texture) return false;
                static const char* kStrata[] = {"BACKGROUND", "LOW", "MEDIUM", "HIGH", "DIALOG", "FULLSCREEN", "FULLSCREEN_DIALOG", "TOOLTIP"};
                return CallOn(L, widget.mainRef, "SetFrameStrata", 1, [&](lua_State* S) { lua_pushstring(S, kStrata[data[0]]); });
            }
            case WasmAddon::UiOp::SetAlpha:
                if (len < 4) return false;
                return CallOn(L, widget.mainRef, "SetAlpha", 1, [&](lua_State* S) { lua_pushnumber(S, ReadI32(data) / 1000.0); });
            case WasmAddon::UiOp::SetMouse:
                if (len < 1) return false;
                return CallOn(L, widget.mainRef, "EnableMouse", 1, [&](lua_State* S) {
                    if (data[0]) lua_pushnumber(S, 1);
                    else lua_pushnil(S);
                });
            case WasmAddon::UiOp::SetTexture: {
                if (widget.kind != WidgetKind::Texture || len < 1) return false;
                std::string path(reinterpret_cast<const char*>(data), len);
                return CallOn(L, widget.mainRef, "SetTexture", 1, [&](lua_State* S) { lua_pushlstring(S, path.data(), path.size()); });
            }
        }
        return false;
    }

    // ʕ •ᴥ•ʔ✿ Curry the signal in a fixed chunk so addon text never becomes Lua source ✿ ʕ •ᴥ•ʔ
    bool InstallUiSignal(lua_State* L, int32_t ref, const char* scriptName, int32_t guest, const std::string& event) {
        int top = lua_gettop(L);
        if (luaL_loadstring(L, "return function(h, ev) return function() WasmAddon_UiSignal(h, ev) end end") != 0) {
            lua_settop(L, top);
            return false;
        }
        if (lua_pcall(L, 0, 1, 0) != 0) { lua_settop(L, top); return false; }
        lua_pushnumber(L, (lua_Number)guest);
        lua_pushlstring(L, event.data(), event.size());
        if (lua_pcall(L, 2, 1, 0) != 0 || !lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
        int closure = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) { lua_settop(L, top); return false; }
        int self = lua_gettop(L);
        lua_getfield(L, self, "SetScript");
        if (!lua_isfunction(L, -1)) { lua_settop(L, top); return false; }
        lua_pushvalue(L, self);
        lua_pushstring(L, scriptName);
        lua_pushvalue(L, closure);
        bool ok = lua_pcall(L, 3, 0, 0) == 0;
        lua_settop(L, top);
        return ok;
    }

    bool DoUiBind(lua_State* L, uint64_t handle, int32_t guest, const std::string& event) {
        auto it = m_ui.find(handle);
        if (it == m_ui.end()) return false;
        TaintScope taint("ui");
        TrackState(L);
        it = m_ui.find(handle);
        if (it == m_ui.end()) return false;
        const char* script = nullptr;
        bool mouse = false;
        bool keyboard = false;
        if (event == "click") { script = "OnClick"; mouse = true; }
        else if (event == "enter") { script = "OnEnter"; mouse = true; }
        else if (event == "leave") { script = "OnLeave"; mouse = true; }
        else if (event == "escape") { script = "OnEscapePressed"; keyboard = true; }
        else if (event == "enter_pressed") { script = "OnEnterPressed"; keyboard = true; }
        else if (event == "text_changed") { script = "OnTextChanged"; keyboard = true; }
        else return false;
        WidgetKind kind = it->second.kind;
        bool frameLike = kind == WidgetKind::Frame || kind == WidgetKind::Button || kind == WidgetKind::StatusBar
            || kind == WidgetKind::EditBox || kind == WidgetKind::ScrollFrame;
        if (!frameLike) return false;
        if (keyboard && kind != WidgetKind::EditBox) return false;
        if (event == "click" && kind != WidgetKind::Button && kind != WidgetKind::Frame && kind != WidgetKind::EditBox
            && kind != WidgetKind::ScrollFrame && kind != WidgetKind::StatusBar) return false;
        int32_t ref = it->second.mainRef;
        if (mouse && !CallOn(L, ref, "EnableMouse", 1, [](lua_State* S) { lua_pushnumber(S, 1); })) return false;
        if (keyboard) {
            if (!CallOn(L, ref, "EnableKeyboard", 1, [](lua_State* S) { lua_pushnumber(S, 1); })) return false;
            if (!CallOn(L, ref, "EnableMouse", 1, [](lua_State* S) { lua_pushnumber(S, 1); })) return false;
        }
        return InstallUiSignal(L, ref, script, guest, event);
    }

    bool DoUiGetText(lua_State* L, uint64_t handle, std::string& out) {
        auto it = m_ui.find(handle);
        if (it == m_ui.end()) return false;
        TaintScope taint("ui");
        TrackState(L);
        it = m_ui.find(handle);
        if (it == m_ui.end()) return false;
        out.clear();
        int32_t ref = it->second.textRef != LUA_NOREF ? it->second.textRef : it->second.mainRef;
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (lua_istable(L, -1) || lua_isuserdata(L, -1)) {
            lua_getfield(L, -1, "GetText");
            if (lua_isfunction(L, -1)) {
                lua_pushvalue(L, -2);
                if (lua_pcall(L, 1, 1, 0) == 0 && lua_type(L, -1) == LUA_TSTRING) {
                    size_t len = 0;
                    const char* text = lua_tolstring(L, -1, &len);
                    if (text) out.assign(text, len);
                }
            }
        }
        lua_settop(L, top);
        return true;
    }

    static bool EndsWithFold(const std::string& path, const char* ext) {
        size_t n = std::strlen(ext);
        if (path.size() < n) return false;
        for (size_t i = 0; i < n; i++) {
            char a = path[path.size() - n + i];
            char b = ext[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }

    static bool InsideRoot(const std::string& root, const fs::path& file) {
        fs::path base = fs::path(root).lexically_normal();
        fs::path norm = file.lexically_normal();
        std::string baseS = base.string();
        std::string fileS = norm.string();
        return fileS.size() > baseS.size() && fileS.compare(0, baseS.size(), baseS) == 0 &&
            (fileS[baseS.size()] == '/' || fileS[baseS.size()] == '\\');
    }

    static std::string GameRelative(const fs::path& file) {
        std::error_code ec;
        fs::path rel = fs::relative(file, fs::current_path(), ec);
        if (ec) return {};
        std::string text = rel.generic_string();
        for (char& c : text) if (c == '/') c = '\\';
        return text;
    }

    int RunLua(const std::string& addonId, const std::string& chunkName, const char* source, size_t length, std::string& error) {
        lua_State* L = FrameScript_GetContext();
        if (!L) { error = "lua state unavailable"; return WasmAddon::ErrUnavailable; }
        if (length > 8u * 1024u * 1024u) { error = "lua source too large"; return WasmAddon::ErrLimitExceeded; }
        TaintScope taint(addonId);
        int top = lua_gettop(L);
        if (luaL_loadbuffer(L, source, length, chunkName.c_str())) {
            error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "lua load error";
            lua_settop(L, top);
            return WasmAddon::ErrGeneric;
        }
        if (lua_pcall(L, 0, 0, 0)) {
            error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "lua runtime error";
            lua_settop(L, top);
            return WasmAddon::ErrGeneric;
        }
        lua_settop(L, top);
        return WasmAddon::Ok;
    }

    int LoadLuaFile(const std::string& addonId, const std::string& addonRoot, const std::string& absolutePath, std::string& error, std::set<std::string>& seen, int depth) {
        if (depth > 8) { error = "toc include depth exceeded"; return WasmAddon::ErrLimitExceeded; }
        fs::path file = fs::path(absolutePath).lexically_normal();
        if (!InsideRoot(addonRoot, file)) { error = "path escapes addon"; return WasmAddon::ErrInvalidArgument; }
        std::string key = file.string();
        if (!seen.insert(key).second) return WasmAddon::Ok;
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) { error = "missing file " + key; return WasmAddon::ErrNotFound; }
        if (EndsWithFold(key, ".toc")) {
            std::ifstream in(file);
            if (!in) { error = "couldn't open toc"; return WasmAddon::ErrNotFound; }
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t start = line.find_first_not_of(" \t");
                if (start == std::string::npos || line[start] == '#') continue;
                size_t end = line.find_last_not_of(" \t");
                std::string name = line.substr(start, end - start + 1);
                fs::path child = (file.parent_path() / name).lexically_normal();
                int rc = LoadLuaFile(addonId, addonRoot, child.string(), error, seen, depth + 1);
                if (rc != WasmAddon::Ok) return rc;
            }
            return WasmAddon::Ok;
        }
        if (EndsWithFold(key, ".xml")) {
            std::string rel = GameRelative(file);
            if (rel.empty()) { error = "xml must live under the client directory: " + key; return WasmAddon::ErrUnavailable; }
            TaintScope taint(addonId);
            CStatus status;
            if (!FrameXML_ProcessFileNoHash(rel.c_str(), addonId.c_str(), &status)) {
                error = "xml failed: " + rel;
                return WasmAddon::ErrGeneric;
            }
            return WasmAddon::Ok;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) { error = "couldn't open " + key; return WasmAddon::ErrNotFound; }
        std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return RunLua(addonId, "@" + key, source.data(), source.size(), error);
    }

    bool DoUiSetText(lua_State* L, uint64_t handle, const std::string& text) {
        auto it = m_ui.find(handle);
        if (it == m_ui.end()) return false;
        TaintScope taint("ui");
        TrackState(L);
        it = m_ui.find(handle);
        if (it == m_ui.end() || it->second.textRef == LUA_NOREF) return false;
        int top = lua_gettop(L);
        lua_rawgeti(L, LUA_REGISTRYINDEX, it->second.textRef);
        if (!lua_istable(L, -1) && !lua_isuserdata(L, -1)) { lua_settop(L, top); return false; }
        lua_getfield(L, -1, "SetText");
        lua_pushvalue(L, -2);
        lua_pushstring(L, text.c_str());
        bool ok = lua_pcall(L, 2, 0, 0) == 0;
        lua_settop(L, top);
        return ok;
    }

    // ʕ •ᴥ•ʔ✿ Registry refs belong to one lua_State; a new state orphans them without luaL_unref ✿ ʕ •ᴥ•ʔ
    void TrackState(lua_State* L) {
        if (m_seenState && L != m_seenState) m_ui.clear();
        m_seenState = L;
    }
    enum class WidgetKind : uint8_t { Frame, FontString, Texture, StatusBar, Button, EditBox, ScrollFrame };
    struct UiRefs { int32_t mainRef = LUA_NOREF; int32_t textRef = LUA_NOREF; WidgetKind kind = WidgetKind::Frame; };
    lua_State* m_seenState = nullptr;
    std::map<uint64_t, UiRefs> m_ui;
    uint64_t m_nextHandle = 1;

    static int32_t MakeFontString(lua_State* L) {
        // ʕ ◕ᴥ◕ ʔ✿ Parent is on top of the stack; returns a registry ref to the new FontString ✿ ʕ ◕ᴥ◕ ʔ
        int32_t parent = lua_gettop(L);
        lua_getfield(L, parent, "CreateFontString");
        lua_pushvalue(L, parent);
        lua_pushnil(L);
        lua_pushstring(L, "OVERLAY");
        lua_pushstring(L, "GameFontNormal");
        if (lua_pcall(L, 4, 1, 0) != 0) return LUA_NOREF;
        lua_getfield(L, -1, "SetPoint");
        lua_pushvalue(L, -2);
        lua_pushstring(L, "CENTER");
        lua_pcall(L, 2, 0, 0);
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }
    static int32_t CallInt(lua_State* L, const char* fn, int32_t a) {
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, fn);
        lua_pushnumber(L, a);
        double v = lua_pcall(L, 1, 1, 0) == 0 ? lua_tonumber(L, -1) : 0;
        int32_t result = v >= 0 && v <= 10000000 ? (int32_t)v : 0;
        lua_settop(L, top);
        return result;
    }
    static int32_t CallInt2(lua_State* L, const char* fn, int32_t a, int32_t b) {
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, fn);
        lua_pushnumber(L, a);
        lua_pushnumber(L, b);
        double v = lua_pcall(L, 2, 1, 0) == 0 ? lua_tonumber(L, -1) : 0;
        int32_t result = v >= 0 && v <= 10000000 ? (int32_t)v : 0;
        lua_settop(L, top);
        return result;
    }
    static int32_t CallSecondInt(lua_State* L, const char* fn, int32_t a, int32_t b) {
        int top = lua_gettop(L);
        lua_getfield(L, LUA_GLOBALSINDEX, fn);
        lua_pushnumber(L, a);
        lua_pushnumber(L, b);
        int32_t result = 0;
        if (lua_pcall(L, 2, LUA_MULTRET, 0) == 0 && lua_gettop(L) >= top + 2)
            result = (int32_t)lua_tonumber(L, top + 2);
        lua_settop(L, top);
        return result;
    }
};

ClientHostServices s_host;
WasmAddon::WasmAddonManager s_manager;
bool s_initialized = false;

void ClientHostServices::ConfirmShow(const WasmAddon::ConfirmRequest& request) {
    std::string name = "addon";
    auto addons = s_manager.GetInspector();
    if (request.addonIndex < addons.size()) name = WasmAddon::SanitizeDisplayText(addons[request.addonIndex].manifest.name);
    WasmConfirmUi_Show(request, WasmAddon::ComposeConfirmText(request, name));
}

void ClientHostServices::ConfirmHide(uint32_t requestId) {
    WasmConfirmUi_Hide(requestId);
}

void OnGameEvent(uint32_t, const char* eventName, lua_State* L, int32_t argCount) {
    if (!L || !s_manager.HasSubscriber(eventName)) return;
    WasmAddon::EventPayloadWriter writer(eventName, 0);
    int32_t top = lua_gettop(L);
    // ʕ ◕ᴥ◕ ʔ✿ Stack: [name, args...]; lua_tolstring is only called on values already of type string ✿ ʕ ◕ᴥ◕ ʔ
    for (int32_t i = 1; i < argCount && i <= (int32_t)WasmAddon::kMaxEventArgs; i++) {
        int32_t idx = top - argCount + 1 + i;
        switch (lua_type(L, idx)) {
            case LUA_TBOOLEAN: writer.Bool(lua_toboolean(L, idx) != 0); break;
            case LUA_TNUMBER: writer.Number(lua_tonumber(L, idx)); break;
            case LUA_TSTRING: {
                size_t len = 0;
                const char* text = lua_tolstring(L, idx, &len);
                writer.String(text ? text : "", text ? len : 0);
                break;
            }
            default: writer.Nil(); break;
        }
    }
    s_manager.DispatchEvent(eventName, writer.Finish());
}

bool OnEventInterest(uint32_t index) {
    if (index >= NUM_SCRIPT_EVENTS) return false;
    const char* name = g_scriptEvents[index];
    return name && s_manager.HasSubscriber(name);
}

int32_t Script_WasmAddonUiSignal(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER || lua_type(L, 2) != LUA_TSTRING) return 0;
    size_t len = 0;
    const char* event = lua_tolstring(L, 2, &len);
    WasmAddon::EventPayloadWriter writer("WASM_UI", 0);
    writer.String(event ? event : "", event ? len : 0);
    writer.Number(lua_tonumber(L, 1));
    s_manager.DispatchEvent("WASM_UI", writer.Finish());
    return 0;
}

int32_t Script_WasmAddonInspector(lua_State* L) {
    std::string text = s_manager.FormatInspectorText();
    lua_pushstring(L, text.c_str());
    return 1;
}

int32_t Script_WasmAddonGetNumAddOns(lua_State* L) {
    lua_pushnumber(L, (lua_Number)s_manager.GetInspector().size());
    return 1;
}

// ʕ ◕ᴥ◕ ʔ✿ Multi-return, mirroring Script_GetAddOnInfo; read-only so tainted panels may poll it freely ✿ ʕ ◕ᴥ◕ ʔ
// ʕ ◕ᴥ◕ ʔ✿ Return order (stable ABI): id, name, version, state, sha256, signaturePresent, memoryBytes, fuelUsed, hostCalls, eventsReceived, traps, denials, uiHandles, lastError, storageAccountBytes, storageCharacterBytes, storageKeyCount ✿ ʕ ◕ᴥ◕ ʔ
int32_t Script_WasmAddonGetAddOnInfo(lua_State* L) {
    auto addons = s_manager.GetInspector();
    int32_t index = 0;
    if (lua_isnumber(L, 1)) {
        index = (int32_t)lua_tonumber(L, 1);
    } else if (lua_isstring(L, 1)) {
        const char* id = lua_tostring(L, 1);
        for (size_t i = 0; i < addons.size(); i++)
            if (addons[i].manifest.id == id) index = (int32_t)i + 1;
    } else {
        return luaL_error(L, "Usage: WasmAddon_GetAddOnInfo(index or addon id)");
    }
    if (index < 1 || (size_t)index > addons.size())
        return luaL_error(L, "AddOn index must be in the range of 1 to %d", (int)addons.size());
    const WasmAddon::InspectorInfo& info = addons[index - 1];
    lua_pushstring(L, info.manifest.id.c_str());
    lua_pushstring(L, WasmAddon::SanitizeDisplayText(info.manifest.name).c_str());
    lua_pushstring(L, WasmAddon::SanitizeDisplayText(info.manifest.version).c_str());
    lua_pushstring(L, WasmAddon::ToString(info.state));
    if (info.sha256.empty()) lua_pushnil(L); else lua_pushstring(L, info.sha256.c_str());
    lua_pushboolean(L, info.signaturePresent);
    lua_pushnumber(L, (lua_Number)info.stats.memoryBytes);
    lua_pushnumber(L, (lua_Number)info.stats.fuelUsed);
    lua_pushnumber(L, (lua_Number)info.stats.hostCalls);
    lua_pushnumber(L, (lua_Number)info.stats.eventsReceived);
    lua_pushnumber(L, (lua_Number)info.stats.traps);
    lua_pushnumber(L, (lua_Number)info.stats.denials);
    lua_pushnumber(L, (lua_Number)info.activeUiHandles);
    if (info.lastError.empty()) lua_pushnil(L); else lua_pushstring(L, WasmAddon::SanitizeDisplayText(info.lastError).c_str());
    lua_pushnumber(L, (lua_Number)info.storageBytes[0]);
    lua_pushnumber(L, (lua_Number)info.storageBytes[1]);
    lua_pushnumber(L, (lua_Number)info.storageKeys[0] + (lua_Number)info.storageKeys[1]);
    return 17;
}

int32_t SetPermissionFromConsole(const char* arguments, bool granted) {
    if (lua_tainted) {
        ConsoleWrite("wasm permission changes are only available from the developer console", ERROR_COLOR);
        return 1;
    }
    char id[128] = {};
    char permName[64] = {};
    if (!arguments || sscanf(arguments, "%127s %63s", id, permName) != 2) {
        ConsoleWrite(granted ? "usage: wasmgrant <addon-id> <permission>" : "usage: wasmdeny <addon-id> <permission>", DEFAULT_COLOR);
        return 0;
    }
    WasmAddon::Permission perm;
    if (!WasmAddon::PermissionFromName(permName, perm)) {
        ConsoleWrite("unknown permission", DEFAULT_COLOR);
        return 0;
    }
    for (auto& info : s_manager.GetInspector()) {
        if (info.manifest.id != id) continue;
        if (!(info.manifest.declaredPermissions & perm)) {
            ConsoleWrite("permission not declared by this addon", DEFAULT_COLOR);
            return 0;
        }
        s_manager.SetPermissionOverride(id, perm, granted);
        ConsoleWrite(granted ? "permission granted" : "permission revoked", DEFAULT_COLOR);
        return 1;
    }
    ConsoleWrite("unknown wasm addon id", DEFAULT_COLOR);
    return 0;
}

int32_t CCommand_WasmGrant(const char*, const char* arguments) { return SetPermissionFromConsole(arguments, true); }
int32_t CCommand_WasmDeny(const char*, const char* arguments) { return SetPermissionFromConsole(arguments, false); }
int32_t CCommand_WasmStorage(const char*, const char* arguments) {
    if (lua_tainted) { ConsoleWrite("wasm storage changes are only available from the developer console", ERROR_COLOR); return 1; }
    char id[128] = {};
    char action[32] = {};
    if (!arguments || sscanf(arguments, "%127s %31s", id, action) < 1) {
        ConsoleWrite("usage: wasmstorage <addon-id> [keys|clear]", ERROR_COLOR);
        return 0;
    }
    for (auto& info : s_manager.GetInspector()) {
        if (info.manifest.id != id) continue;
        if (std::string(action) == "clear") {
            s_manager.ClearStorage(id, 0);
            s_manager.ClearStorage(id, 1);
            ConsoleWrite("wasm storage cleared", DEFAULT_COLOR);
        } else {
            char line[256];
            snprintf(line, sizeof(line), "%s storage: account %lluB/%u keys, character %lluB/%u keys", id,
                     (unsigned long long)info.storageBytes[0], info.storageKeys[0],
                     (unsigned long long)info.storageBytes[1], info.storageKeys[1]);
            ConsoleWrite(line, DEFAULT_COLOR);
        }
        return 1;
    }
    ConsoleWrite("unknown wasm addon id", ERROR_COLOR);
    return 0;
}

int32_t CCommand_WasmConfirm(const char*, const char* arguments) {
    if (lua_tainted) {
        ConsoleWrite("wasm confirm is only available from the developer console", ERROR_COLOR);
        return 1;
    }
    unsigned id = 0;
    char action[16] = {};
    if (!arguments || sscanf(arguments, "%u %15s", &id, action) != 2) {
        ConsoleWrite("usage: wasmconfirm <id> accept|deny", DEFAULT_COLOR);
        return 0;
    }
    bool accept = std::string(action) == "accept";
    if (!accept && std::string(action) != "deny") {
        ConsoleWrite("usage: wasmconfirm <id> accept|deny", DEFAULT_COLOR);
        return 0;
    }
    if (!s_manager.ResolveConfirm(id, accept)) {
        ConsoleWrite("no matching wasm confirm request", DEFAULT_COLOR);
        return 0;
    }
    ConsoleWrite(accept ? "wasm confirm accepted" : "wasm confirm denied", DEFAULT_COLOR);
    return 1;
}

int32_t CCommand_WasmAddons(const char*, const char*) {
    std::string text = s_manager.FormatInspectorText();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        ConsoleWrite(text.substr(pos, eol == std::string::npos ? eol : eol - pos).c_str(), DEFAULT_COLOR);
        if (eol == std::string::npos) break;
        pos = eol + 1;
    }
    return 1;
}

} // namespace

void WasmAddonClient_ServiceConfirms() {
    if (!s_initialized) return;
    uint64_t now = OsGetAsyncTimeMs();
    WasmConfirmUi_Tick(now);
    s_manager.TickConfirm();
    s_manager.TickUi(now);
    WasmConfirmUiResult result;
    if (WasmConfirmUi_Poll(result)) s_manager.ResolveConfirm(result.id, result.accepted);
}

void WasmAddonClient_OnScriptReset() {
    if (!s_initialized) return;
    s_host.DropUiReferences();
    s_manager.InvalidateUiHandles();
    WasmAddonClient_RegisterScripts();
}

void WasmAddonClient_Initialize() {
    if (s_initialized) return;
    std::string root = std::filesystem::exists("Interface/WasmAddOns") ? "Interface/WasmAddOns" : "WasmAddOns";
    s_manager.Initialize(root, &s_host, "WTF/wasm-permissions.cfg");
    FrameScript_SetEventObserver(OnGameEvent, OnEventInterest);
    ConsoleCommandRegister("wasmgrant", CCommand_WasmGrant, CATEGORY_DEBUG, "wasmgrant <addon-id> <permission>");
    ConsoleCommandRegister("wasmdeny", CCommand_WasmDeny, CATEGORY_DEBUG, "wasmdeny <addon-id> <permission>");
    ConsoleCommandRegister("wasmaddons", CCommand_WasmAddons, CATEGORY_DEBUG, "list WASM addons");
    ConsoleCommandRegister("wasmstorage", CCommand_WasmStorage, CATEGORY_DEBUG, "wasmstorage <addon-id> [keys|clear]");
    ConsoleCommandRegister("wasmconfirm", CCommand_WasmConfirm, CATEGORY_DEBUG, "wasmconfirm <id> accept|deny");
    s_initialized = true;
}

// ʕ ◕ᴥ◕ ʔ✿ Lua states are rebuilt at glue/game transitions, so registration follows Load/UnloadScriptFunctions ✿ ʕ ◕ᴥ◕ ʔ
void WasmAddonClient_RegisterScripts() {
    if (!s_initialized) return;
    FrameScript_RegisterFunction("WasmAddonInspectorText", Script_WasmAddonInspector);
    FrameScript_RegisterFunction("WasmAddon_GetNumAddOns", Script_WasmAddonGetNumAddOns);
    FrameScript_RegisterFunction("WasmAddon_GetAddOnInfo", Script_WasmAddonGetAddOnInfo);
    FrameScript_RegisterFunction("WasmAddon_UiSignal", Script_WasmAddonUiSignal);
}
void WasmAddonClient_UnregisterScripts() {
    if (!s_initialized) return;
    FrameScript_UnregisterFunction("WasmAddonInspectorText");
    FrameScript_UnregisterFunction("WasmAddon_GetNumAddOns");
    FrameScript_UnregisterFunction("WasmAddon_GetAddOnInfo");
    FrameScript_UnregisterFunction("WasmAddon_UiSignal");
}

// ʕ •ᴥ•ʔ✿ Loading mirrors LoadAddOns: UI/chat/inventory services only exist once the game UI is up ✿ ʕ •ᴥ•ʔ
void WasmAddonClient_LoadWorldAddOns() {
    if (!s_initialized) return;
    // ʕ •ᴥ•ʔ✿ Storage lives under WTF account/character dirs, never in the addon install folder ✿ ʕ •ᴥ•ʔ
    std::string account = ClientServices::GetAccountName() ? ClientServices::GetAccountName() : "";
    if (account.empty()) account = "unknown";
    std::string accountDir = "WTF/Account/" + account + "/WasmAddOns";
    std::string charDir;
    const char* realm = ClientServices::GetSelectedRealmName();
    const char* character = ClientServices::GetCharacterName();
    if (realm && *realm && character && *character)
        charDir = "WTF/Account/" + account + "/" + realm + "/" + character + "/WasmAddOns";
    s_manager.SetStorageDirs(accountDir, charDir);
    s_manager.ScanAndLoadAll();
}
void WasmAddonClient_UnloadWorldAddOns() {
    if (!s_initialized) return;
    s_manager.SetStorageDirs("", "");
    for (auto& info : s_manager.GetInspector())
        if (info.state != WasmAddon::AddonState::Unloaded) s_manager.UnloadAddon(info.manifest.id);
}

void WasmAddonClient_Shutdown() {
    if (!s_initialized) return;
    FrameScript_SetEventObserver(nullptr, nullptr);
    ConsoleCommandUnregister("wasmgrant");
    ConsoleCommandUnregister("wasmdeny");
    ConsoleCommandUnregister("wasmaddons");
    ConsoleCommandUnregister("wasmstorage");
    ConsoleCommandUnregister("wasmconfirm");
    WasmAddonClient_UnloadWorldAddOns();
    s_manager.Shutdown();
    s_initialized = false;
}
