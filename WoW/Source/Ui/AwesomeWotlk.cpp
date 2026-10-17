#include "AwesomeWotlk.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemClassRec.h"
#include "WoW/Source/DB/Rec/ItemSubClassRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/ObjectClient/GameObject_C.h"
#include "WoW/Source/Object/ObjectClient/Item_C.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/Types.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/NamePlateFrame.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/Ui/Types.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Tooltip.h"
#if defined(WOWDECOMP_LINUX)
#include <SDL3/SDL.h>
#elif !defined(_WIN32)
#include <SDL2/SDL.h>
#endif

// ʕ •ᴥ•ʔ✿ One implementation serves the Lua globals and the WASM whitelist ✿ ʕ •ᴥ•ʔ

static const uint32_t kControlled = 0x00C60000;
static const uint32_t kDisarmed = 0x00200000;
static const uint32_t kSilenced = 0x00002000;
static const uint32_t kSkinnable = 0x04000000;
static const float kInteractYards = 20.0f;

static const char* kCalls[] = {
    "C_NamePlate.GetNamePlateForUnit", "C_NamePlate.GetNamePlates", "C_NamePlate.GetNamePlateByGUID", "C_NamePlate.GetNamePlateTokenByGUID",
    "C_VoiceChat.GetTtsVoices", "C_VoiceChat.GetRemoteTtsVoices", "C_VoiceChat.SpeakText", "C_VoiceChat.StopSpeakingText",
    "C_TTSSettings.GetSpeechRate", "C_TTSSettings.GetSpeechVolume", "C_TTSSettings.GetSpeechVoiceID", "C_TTSSettings.GetVoiceOptionName",
    "C_TTSSettings.SetDefaultSettings", "C_TTSSettings.SetSpeechRate", "C_TTSSettings.SetSpeechVolume", "C_TTSSettings.SetVoiceOption",
    "C_TTSSettings.SetVoiceOptionByName", "C_TTSSettings.RefreshVoices",
    "UnitIsControlled", "UnitIsDisarmed", "UnitIsSilenced", "UnitOccupations", "UnitOwner", "UnitTokenFromGUID",
    "GetInventoryItemTransmog", "GetSpellBaseCooldown", "GetItemInfoInstant",
    "FlashWindow", "IsWindowFocused", "FocusWindow", "CopyToClipboard", "QueueInteract",
    "GetCVar", "SetCVar",
};

static const char* kCVars[] = {
    "nameplateDistance", "nameplateStacking", "nameplateXSpace", "nameplateYSpace", "nameplateUpperBorder", "nameplateOriginPos",
    "nameplateSpeedRaise", "nameplateSpeedReset", "nameplateSpeedLower",
    "nameplateFriendlyHitboxHeight", "nameplateFriendlyHitboxWidth", "nameplateHitboxHeight", "nameplateHitboxWidth",
    "nameplateStackFriendly", "nameplateStackFriendlyMode", "nameplateMaxRaiseDistance", "nameplateExtendWorldFrameHeight", "nameplateUpperBorderOnlyBoss",
    "ttsVoice", "ttsSpeed", "ttsVolume",
    "cameraIndirectVisibility", "cameraIndirectAlpha", "interactionMode", "interactionAngle", "MSDFMode", "cameraFov", "showPlayer", "enableStancePatch",
};

static bool s_registered = false;
static bool s_slash = false;
static int s_showApplied = -1;
static int s_ground = 0;
static int s_groundAge = 0;
static WOWGUID s_interact;

struct PlateSlot {
    CGNamePlateFrame* owner = nullptr;
    CGNamePlateFrame* shown = nullptr;
    WOWGUID guid;
    int id = 0;
    bool announced = false;
    char token[16] = {};
};

static PlateSlot s_plates[64];
static int s_nextPlate = 0;

struct Voice {
    int id;
    char name[48];
};

static Voice s_voices[32];
static int s_voiceCount = 0;

struct SpeechJob {
    uint32_t id = 0;
    int dest = 1;
    int rate = 0;
    int volume = 100;
    char voice[48] = {};
    char text[400] = {};
#if !defined(_WIN32)
    pid_t pid = 0;
#endif
};

static SpeechJob s_live;
static bool s_liveOn = false;
static SpeechJob s_speechQueue[4];
static int s_speechCount = 0;
static uint32_t s_utterance = 0;

static bool Exact(const char* a, const char* b) {
    return a && b && !strcmp(a, b);
}

bool AwesomeWotlkIsCall(const char* name) {
    if (!name) return false;
    for (const char* call : kCalls) if (Exact(call, name)) return true;
    return false;
}

bool AwesomeWotlkIsCVar(const char* name) {
    if (!name) return false;
    for (const char* cvar : kCVars) if (Exact(cvar, name)) return true;
    return false;
}

static int CVarInt(const char* name, int fallback) {
    CVar* cvar = CVar::Lookup(name);
    return cvar ? cvar->m_intValue : fallback;
}

static void SetCVarInt(const char* name, int value) {
    CVar* cvar = CVar::Lookup(name);
    if (cvar) cvar->Set(value, true, false, false, true);
}

static int Clamp(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void FormatGuid(char* out, size_t size, uint64_t guid) {
    static const char* hex = "0123456789abcdef";
    char text[19];
    text[0] = '0';
    text[1] = 'x';
    for (int i = 0; i < 16; i++) text[2 + i] = hex[(guid >> (60 - i * 4)) & 0xF];
    text[18] = 0;
    SStrCopy(out, text, size);
}

static bool ParseGuidText(const char* text, WOWGUID& guid) {
    if (!text || !*text) return false;
    char* end = nullptr;
    unsigned long long value = strtoull(text, &end, 0);
    if (end && end != text && *end == 0) {
        guid.m_guid = value;
        return guid != WOWGUID();
    }
    const char* cursor = text;
    return Script_GetGUIDFromString(&cursor, guid) && guid != WOWGUID();
}

static PlateSlot* PlateByOwner(CGNamePlateFrame* frame) {
    for (PlateSlot& slot : s_plates) if (slot.owner == frame) return &slot;
    return nullptr;
}

static PlateSlot* PlateById(int id) {
    for (PlateSlot& slot : s_plates) if (slot.id == id && slot.owner) return &slot;
    return nullptr;
}

static PlateSlot* PlateByGuid(const WOWGUID& guid) {
    if (guid == WOWGUID()) return nullptr;
    for (PlateSlot& slot : s_plates) if (slot.shown && slot.guid == guid) return &slot;
    return nullptr;
}

static PlateSlot* AllocPlate(CGNamePlateFrame* frame) {
    PlateSlot* slot = PlateByOwner(frame);
    if (slot) return slot;
    for (PlateSlot& candidate : s_plates) {
        if (!candidate.owner) {
            slot = &candidate;
            break;
        }
    }
    if (!slot) return nullptr;
    slot->owner = frame;
    slot->id = ++s_nextPlate;
    SStrPrintf(slot->token, sizeof(slot->token), "nameplate%d", slot->id);
    return slot;
}

const char* AwesomeWotlkTokenForPointer(const void* pointer) {
    if (!pointer) return nullptr;
    PlateSlot* slot = PlateByOwner(static_cast<CGNamePlateFrame*>(const_cast<void*>(pointer)));
    return slot ? slot->token : nullptr;
}

const char* AwesomeWotlkConsumeNameplateToken(const char* token, WOWGUID& guid) {
    const char* cursor = token + 9;
    int id = 0;
    while (*cursor >= '0' && *cursor <= '9' && id < 100000) {
        id = id * 10 + (*cursor - '0');
        cursor++;
    }
    guid = WOWGUID();
    PlateSlot* slot = PlateById(id);
    if (slot && slot->shown) guid = slot->guid;
    return cursor;
}

static void PushPlate(lua_State* L, CGNamePlateFrame* frame) {
    if (!frame) {
        lua_pushnil(L);
        return;
    }
    if (frame->m_luaObjectRef == LUA_NOREF) frame->RegisterScriptObject(nullptr);
    if (frame->m_luaObjectRef == LUA_NOREF) {
        lua_pushnil(L);
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, frame->m_luaObjectRef);
}

static void SignalPlate(CGNamePlateFrame* frame) {
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    int top = lua_gettop(L);
    lua_pushstring(L, "NAME_PLATE_CREATED");
    PushPlate(L, frame);
    FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_CREATED, L, 2);
    lua_settop(L, top);
}

void AwesomeWotlkOnNamePlateAdded(CGNamePlateFrame* frame) {
    if (!frame) return;
    PlateSlot* slot = AllocPlate(frame);
    if (!slot) return;
    WOWGUID previous = slot->guid;
    bool ownerChanged = slot->announced && previous != WOWGUID() && previous != frame->m_guid;
    slot->shown = frame;
    slot->guid = frame->m_guid;
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    if (!slot->announced) {
        SignalPlate(frame);
        slot->announced = frame->m_luaObjectRef != LUA_NOREF;
    } else if (ownerChanged) {
        FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_OWNER_CHANGED, "%s", slot->token);
    }
    FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_UNIT_ADDED, "%s", slot->token);
}

void AwesomeWotlkOnNamePlateRemoved(CGNamePlateFrame* frame) {
    PlateSlot* slot = frame ? PlateByOwner(frame) : nullptr;
    if (!slot || slot->shown != frame) return;
    if (FrameScript_GetContext()) FrameScript_SignalEvent(SCRIPT_EVENT_NAME_PLATE_UNIT_REMOVED, "%s", slot->token);
    slot->shown = nullptr;
}

static int32_t Script_GetNamePlates(lua_State* L) {
    lua_newtable(L);
    int index = 1;
    for (const PlateSlot& slot : s_plates) {
        if (!slot.shown) continue;
        PushPlate(L, slot.shown);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

static int32_t Script_GetNamePlateForUnit(lua_State* L) {
    if (!lua_isstring(L, 1)) return 0;
    CGUnit_C* unit = GetUnitFromName(lua_tostring(L, 1));
    PlateSlot* slot = unit && unit->m_obj ? PlateByGuid(unit->m_obj->m_guid) : nullptr;
    if (!slot) return 0;
    PushPlate(L, slot->shown);
    return 1;
}

static int32_t Script_GetNamePlateByGUID(lua_State* L) {
    if (!lua_isstring(L, 1)) return 0;
    WOWGUID guid;
    if (!ParseGuidText(lua_tostring(L, 1), guid)) return 0;
    PlateSlot* slot = PlateByGuid(guid);
    if (!slot) return 0;
    PushPlate(L, slot->shown);
    return 1;
}

static int32_t Script_GetNamePlateTokenByGUID(lua_State* L) {
    if (!lua_isstring(L, 1)) return 0;
    WOWGUID guid;
    if (!ParseGuidText(lua_tostring(L, 1), guid)) return 0;
    PlateSlot* slot = PlateByGuid(guid);
    if (!slot) return 0;
    lua_pushstring(L, slot->token);
    return 1;
}

static CGUnit_C* UnitArg(lua_State* L) {
    if (!lua_isstring(L, 1)) return nullptr;
    CGUnit_C* unit = GetUnitFromName(lua_tostring(L, 1));
    return unit && unit->m_unit ? unit : nullptr;
}

static int32_t PushMasked(lua_State* L, uint32_t mask) {
    CGUnit_C* unit = UnitArg(L);
    if (!unit || !(unit->m_unit->m_unitFieldFlags & mask)) return 0;
    lua_pushnumber(L, 1);
    return 1;
}

static int32_t Script_UnitIsControlled(lua_State* L) { return PushMasked(L, kControlled); }
static int32_t Script_UnitIsDisarmed(lua_State* L) { return PushMasked(L, kDisarmed); }
static int32_t Script_UnitIsSilenced(lua_State* L) { return PushMasked(L, kSilenced); }

static int32_t Script_UnitOccupations(lua_State* L) {
    CGUnit_C* unit = UnitArg(L);
    if (!unit) return 0;
    lua_pushnumber(L, unit->m_unit->m_unitNpcFlags);
    return 1;
}

static int32_t Script_UnitOwner(lua_State* L) {
    CGUnit_C* unit = UnitArg(L);
    if (!unit) return 0;
    WOWGUID owner = unit->m_unit->m_unitFieldSummonedby;
    if (owner == WOWGUID()) owner = unit->m_unit->m_unitFieldCreatedby;
    if (owner == WOWGUID()) return 0;
    const char* name = GetObjectNameFromGuid(owner);
    char text[24];
    FormatGuid(text, sizeof(text), owner.m_guid);
    lua_pushstring(L, name ? name : "");
    lua_pushstring(L, text);
    return 2;
}

static int32_t Script_UnitTokenFromGUID(lua_State* L) {
    if (!lua_isstring(L, 1)) return 0;
    WOWGUID guid;
    if (!ParseGuidText(lua_tostring(L, 1), guid)) return 0;
    const char* token = Script_GetTokenFromGUID(&guid);
    char copy[32];
    if (token) SStrCopy(copy, token, sizeof(copy));
    if (!token) {
        PlateSlot* slot = PlateByGuid(guid);
        token = slot ? slot->token : nullptr;
    } else {
        token = copy;
    }
    if (!token) return 0;
    lua_pushstring(L, token);
    return 1;
}

static int32_t Script_GetInventoryItemTransmog(lua_State* L) {
    CGUnit_C* unit = UnitArg(L);
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (!unit || !player || unit != player || !player->m_player || !lua_isnumber(L, 2)) return 0;
    int slot = static_cast<int>(lua_tonumber(L, 2)) - 1;
    if (slot < 0 || slot > 18) return 0;
    const VisibleItem_C& item = player->m_player->m_playerVisibleItems[slot];
    lua_pushnumber(L, item.m_itemId);
    lua_pushnumber(L, item.m_enchantment[0]);
    return 2;
}

static int32_t RecoveryMs(int32_t spellId, int depth) {
    if (spellId <= 0 || depth > 2) return 0;
    SpellRec spell;
    if (!g_spellDB.GetRecord(spellId, &spell)) return 0;
    int32_t cooldown = spell.m_recoveryTime ? spell.m_recoveryTime : spell.m_categoryRecoveryTime;
    if (cooldown) return cooldown;
    for (int i = 0; i < 3; i++) {
        int32_t nested = RecoveryMs(spell.m_effectTriggerSpell[i], depth + 1);
        if (nested) return nested;
    }
    return 0;
}

static int32_t Script_GetSpellBaseCooldown(lua_State* L) {
    if (!lua_isnumber(L, 1)) return 0;
    int32_t spellId = static_cast<int32_t>(lua_tonumber(L, 1));
    SpellRec spell;
    if (!g_spellDB.GetRecord(spellId, &spell)) return 0;
    lua_pushnumber(L, RecoveryMs(spellId, 0));
    lua_pushnumber(L, spell.m_startRecoveryTime);
    return 2;
}

static int32_t Script_GetItemInfoInstant(lua_State* L) {
    ItemLinkInfo link;
    link.Clear();
    int32_t itemId = 0;
    if (lua_isnumber(L, 1)) {
        itemId = static_cast<int32_t>(lua_tonumber(L, 1));
    } else if (lua_isstring(L, 1)) {
        const char* text = lua_tostring(L, 1);
        itemId = text && SStrStr(text, "item:") ? link.GetItemIdFromLink(text) : GetItemIDByName(text);
    } else {
        return 0;
    }
    ItemStats_C* stats = g_itemDBCache.GetRecord(itemId, WOWGUID(), nullptr, nullptr, false);
    if (!stats) return 0;
    ItemClassRec* itemClass = g_itemClassDB.GetRecord(stats->m_class);
    const ItemSubClassRec* subClass = nullptr;
    for (int32_t i = 0; i < g_itemSubClassDB.m_numRecords; i++) {
        const ItemSubClassRec* rec = &g_itemSubClassDB.m_records[i];
        if (rec->m_classID == static_cast<int32_t>(stats->m_class) && rec->m_subClassID == static_cast<int32_t>(stats->m_subClass)) {
            subClass = rec;
            break;
        }
    }
    const char* equip = stats->m_inventoryType < 29 ? s_inventoryTypeTokens[stats->m_inventoryType] : "";
    const char* path = ClientDBStringLookup(3);
    const char* art = CGItem_C::GetInventoryArt(static_cast<int32_t>(stats->m_displayId));
    char texture[260];
    SStrPrintf(texture, sizeof(texture), "%s%s%s", path ? path : "", path && *path ? "\\" : "", art ? art : "");
    lua_pushnumber(L, itemId);
    lua_pushstring(L, itemClass && itemClass->m_className ? itemClass->m_className : "");
    const char* subName = "";
    if (subClass && subClass->m_verboseName && *subClass->m_verboseName) subName = subClass->m_verboseName;
    else if (subClass && subClass->m_displayName) subName = subClass->m_displayName;
    lua_pushstring(L, subName);
    lua_pushstring(L, equip ? equip : "");
    lua_pushstring(L, texture);
    lua_pushnumber(L, stats->m_class);
    lua_pushnumber(L, stats->m_subClass);
    return 7;
}

#if !defined(_WIN32)
static SDL_Window* ClientWindow() {
    if (!g_theGxDevicePtr) return nullptr;
    return static_cast<SDL_Window*>(g_theGxDevicePtr->DeviceWindow());
}
#endif

static int32_t Script_FlashWindow(lua_State*) {
#if !defined(_WIN32)
    if (SDL_Window* window = ClientWindow()) SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);
#endif
    return 0;
}

static int32_t Script_IsWindowFocused(lua_State* L) {
#if !defined(_WIN32)
    SDL_Window* window = ClientWindow();
    if (window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
        lua_pushnumber(L, 1);
        return 1;
    }
#else
    (void)L;
#endif
    return 0;
}

static int32_t Script_FocusWindow(lua_State*) {
#if !defined(_WIN32)
    if (SDL_Window* window = ClientWindow()) SDL_RaiseWindow(window);
#endif
    return 0;
}

static int32_t Script_CopyToClipboard(lua_State* L) {
    if (!lua_isstring(L, 1)) return 0;
    size_t length = 0;
    const char* text = lua_tolstring(L, 1, &length);
    if (!text || !length || length > 8192) return 0;
    OsClipboardPutString(text);
    return 0;
}

static bool InteractableType(int type) {
    switch (type) {
        case 0: case 1: case 2: case 3: case 4: case 7: case 8: case 10:
        case 17: case 18: case 19: case 23: case 24: case 26: case 32: case 34: case 35:
            return true;
        default:
            return false;
    }
}

static bool ConsiderInteract(CGObject_C* object, CGPlayer_C* player, bool cone, float cosHalf, float& bestDist, WOWGUID& best) {
    if (!object || !object->m_obj || object->m_obj->m_guid == player->m_obj->m_guid) return false;
    C3Vector from = player->GetPosition();
    C3Vector to = object->GetPosition();
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float dist = dx * dx + dy * dy + dz * dz;
    if (dist > kInteractYards * kInteractYards || dist >= bestDist) return false;
    bool unit = (object->m_obj->m_type & TYPEMASK_UNIT) != 0;
    if (unit) {
        CGUnit_C* target = static_cast<CGUnit_C*>(object);
        if (!target->m_unit) return false;
        bool loot = (target->m_unit->m_unitDynamicFlags & 1) != 0;
        bool skin = (target->m_unit->m_unitFieldFlags & kSkinnable) != 0;
        bool npc = player->CanAssist(target, false) && target->m_unit->m_unitNpcFlags;
        if (!loot && !skin && !npc) return false;
    } else if (object->m_obj->m_type & TYPEMASK_GAMEOBJECT) {
        CGGameObject_C* go = static_cast<CGGameObject_C*>(object);
        if (!go->m_gameObject || !InteractableType(go->m_gameObject->m_typeID) || !go->CanUseNow()) return false;
    } else {
        return false;
    }
    if (cone && dist > 0.25f) {
        float flat = sqrtf(dx * dx + dy * dy);
        if (flat > 0.001f) {
            float facing = player->GetFacing();
            float dot = (dx / flat) * cosf(facing) + (dy / flat) * sinf(facing);
            if (dot < cosHalf) return false;
        }
    }
    bestDist = dist;
    best = object->m_obj->m_guid;
    return true;
}

struct InteractPick {
    CGPlayer_C* player;
    WOWGUID only;
    bool filter;
    bool cone;
    float cosHalf;
    float bestDist;
    WOWGUID best;
};

static int32_t InteractEnum(WOWGUID guid, void* param) {
    InteractPick* pick = static_cast<InteractPick*>(param);
    if (pick->filter && guid != pick->only) return 1;
    CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_UNIT, __FILE__, __LINE__);
    if (!object) object = ClntObjMgrObjectPtr(guid, TYPEMASK_GAMEOBJECT, __FILE__, __LINE__);
    ConsiderInteract(object, pick->player, pick->cone, pick->cosHalf, pick->bestDist, pick->best);
    return 1;
}

static void QueueInteractToken(const char* token) {
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (!player || !player->m_obj) return;
    InteractPick pick;
    pick.player = player;
    pick.filter = false;
    pick.only = WOWGUID();
    pick.cone = CVarInt("interactionMode", 1) != 0;
    float angle = 60.0f;
    if (CVar* cvar = CVar::Lookup("interactionAngle")) angle = cvar->m_floatValue;
    angle = angle < 1.0f ? 1.0f : (angle > 180.0f ? 180.0f : angle);
    pick.cosHalf = cosf(angle * 0.5f * 3.14159265f / 180.0f);
    pick.bestDist = kInteractYards * kInteractYards + 1.0f;
    pick.best = WOWGUID();
    if (token && *token) {
        if (!Script_GetGUIDFromToken(token, pick.only, false)) return;
        pick.filter = true;
    }
    ClntObjMgrEnumVisibleObjects(InteractEnum, &pick);
    if (pick.best != WOWGUID()) s_interact = pick.best;
}

static int32_t Script_QueueInteract(lua_State* L) {
    QueueInteractToken(lua_isstring(L, 1) ? lua_tostring(L, 1) : nullptr);
    return 0;
}

static int32_t Script_InteractSlash(lua_State* L) {
    const char* message = lua_isstring(L, 1) ? lua_tostring(L, 1) : "";
    int top = lua_gettop(L);
    lua_getfield(L, LUA_GLOBALSINDEX, "SecureCmdOptionParse");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, message);
        if (lua_pcall(L, 1, 2, 0) == 0) {
            const char* target = lua_tostring(L, -1);
            QueueInteractToken(target);
            lua_settop(L, top);
            return 0;
        }
    }
    lua_settop(L, top);
    QueueInteractToken(message && *message ? message : nullptr);
    return 0;
}

static void RunInteract() {
    if (s_interact == WOWGUID()) return;
    WOWGUID guid = s_interact;
    s_interact = WOWGUID();
    CGObject_C* object = ClntObjMgrObjectPtr(guid, TYPEMASK_UNIT, __FILE__, __LINE__);
    if (!object) object = ClntObjMgrObjectPtr(guid, TYPEMASK_GAMEOBJECT, __FILE__, __LINE__);
    if (object) object->OnRightClick();
}

static int SpeechRate() { return Clamp(CVarInt("ttsSpeed", 0), -10, 10); }
static int SpeechVolume() { return Clamp(CVarInt("ttsVolume", 100), 0, 100); }
static int SpeechVoice() { return CVarInt("ttsVoice", 1); }

static void FireSpeech(uint32_t event, uint32_t id, int dest, const char* status) {
    if (!FrameScript_GetContext()) return;
    if (status) FrameScript_SignalEvent(event, "%s%f%f", status, static_cast<double>(id), static_cast<double>(dest));
    else if (event == SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_STARTED)
        FrameScript_SignalEvent(event, "%f%f%f%f", 1.0, static_cast<double>(id), 0.0, static_cast<double>(dest));
    else
        FrameScript_SignalEvent(event, "%f%f%f", 1.0, static_cast<double>(id), static_cast<double>(dest));
}

static const char* VoiceName(int id) {
    for (int i = 0; i < s_voiceCount; i++) if (s_voices[i].id == id) return s_voices[i].name;
    return "";
}

#if !defined(_WIN32)
static const char* SpeechProgram(bool& spd) {
    if (access("/usr/bin/spd-say", X_OK) == 0) { spd = true; return "/usr/bin/spd-say"; }
    spd = false;
    if (access("/usr/bin/espeak-ng", X_OK) == 0) return "/usr/bin/espeak-ng";
    if (access("/usr/bin/espeak", X_OK) == 0) return "/usr/bin/espeak";
    return nullptr;
}

static void SanitizeSpeech(char* dest, size_t size, const char* text) {
    size_t n = 0;
    if (!text) text = "";
    for (const char* p = text; *p && n + 1 < size && n < 399; p++) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c >= 32) dest[n++] = static_cast<char>(c);
    }
    dest[n] = 0;
}

static bool StartSpeech(const SpeechJob& job) {
    bool spd = false;
    const char* program = SpeechProgram(spd);
    if (!program) return false;
    char rate[16];
    char volume[16];
    SStrPrintf(rate, sizeof(rate), "%d", spd ? job.rate * 10 : 175 + job.rate * 8);
    SStrPrintf(volume, sizeof(volume), "%d", spd ? job.volume : job.volume * 2);
    char* argv[10];
    int argc = 0;
    argv[argc++] = const_cast<char*>(program);
    if (spd) argv[argc++] = const_cast<char*>("-w");
    argv[argc++] = const_cast<char*>(spd ? "-r" : "-s");
    argv[argc++] = rate;
    argv[argc++] = const_cast<char*>(spd ? "-i" : "-a");
    argv[argc++] = volume;
    bool named = job.voice[0] && strcmp(job.voice, "default");
    if (named) {
        argv[argc++] = const_cast<char*>(spd ? "-l" : "-v");
        argv[argc++] = const_cast<char*>(job.voice);
    }
    argv[argc++] = const_cast<char*>(job.text);
    argv[argc] = nullptr;
    pid_t pid = 0;
    if (posix_spawn(&pid, program, nullptr, nullptr, argv, environ) != 0) return false;
    s_live = job;
    s_live.pid = pid;
    s_liveOn = true;
    FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_STARTED, job.id, job.dest, nullptr);
    return true;
}
#endif

static void EnqueueSpeech(const SpeechJob& job) {
#if !defined(_WIN32)
    if (s_liveOn) {
        if (s_speechCount < 4) s_speechQueue[s_speechCount++] = job;
        else FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, job.id, job.dest, "busy");
        return;
    }
    if (!StartSpeech(job)) FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, job.id, job.dest, "failed");
#else
    FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, job.id, job.dest, "unavailable");
#endif
}

static void ServiceSpeech() {
#if !defined(_WIN32)
    if (s_liveOn) {
        int status = 0;
        pid_t done = waitpid(s_live.pid, &status, WNOHANG);
        if (done == 0) return;
        uint32_t id = s_live.id;
        int dest = s_live.dest;
        s_liveOn = false;
        FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FINISHED, id, dest, nullptr);
    }
    if (!s_liveOn && s_speechCount) {
        SpeechJob next = s_speechQueue[0];
        for (int i = 1; i < s_speechCount; i++) s_speechQueue[i - 1] = s_speechQueue[i];
        s_speechCount--;
        if (!StartSpeech(next)) FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, next.id, next.dest, "failed");
    }
#endif
}

static void ReadVoices(Voice* out, int& count) {
    count = 0;
#if !defined(_WIN32)
    bool spd = false;
    const char* program = SpeechProgram(spd);
    const char* command = nullptr;
    if (program && spd) command = "/usr/bin/spd-say -L";
    else if (program && !strcmp(program, "/usr/bin/espeak-ng")) command = "/usr/bin/espeak-ng --voices";
    else if (program) command = "/usr/bin/espeak --voices";
    if (command) {
        FILE* pipe = popen(command, "r");
        if (pipe) {
            char line[256];
            bool header = true;
            while (count < 32 && fgets(line, sizeof(line), pipe)) {
                if (header) { header = false; continue; }
                char* cursor = line;
                int field = 0;
                char name[48] = {};
                while (*cursor && *cursor != '\n') {
                    while (*cursor == ' ' || *cursor == '\t') cursor++;
                    if (!*cursor || *cursor == '\n') break;
                    char token[48];
                    int n = 0;
                    while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n' && n < 47) token[n++] = *cursor++;
                    token[n] = 0;
                    if (++field == (spd ? 1 : 4)) SStrCopy(name, token, sizeof(name));
                }
                if (!name[0]) continue;
                out[count].id = count + 1;
                SStrCopy(out[count].name, name, sizeof(out[count].name));
                count++;
            }
            pclose(pipe);
        }
    }
#endif
    if (!count) {
        out[0].id = 1;
        SStrCopy(out[0].name, "default", sizeof(out[0].name));
        count = 1;
    }
}

static void EnsureVoices() {
    if (!s_voiceCount) ReadVoices(s_voices, s_voiceCount);
}

static int32_t PushVoices(lua_State* L) {
    EnsureVoices();
    lua_newtable(L);
    for (int i = 0; i < s_voiceCount; i++) {
        lua_newtable(L);
        lua_pushnumber(L, s_voices[i].id);
        lua_setfield(L, -2, "voiceID");
        lua_pushstring(L, s_voices[i].name);
        lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int32_t Script_GetTtsVoices(lua_State* L) { return PushVoices(L); }

static int32_t Script_SpeakText(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isstring(L, 2)) {
        luaL_error(L, "Usage: C_VoiceChat.SpeakText(voiceID, text[, destination, rate, volume])");
        return 0;
    }
    SpeechJob job;
    job.id = ++s_utterance;
    job.dest = lua_isnumber(L, 3) ? static_cast<int>(lua_tonumber(L, 3)) : 1;
    job.rate = lua_isnumber(L, 4) ? Clamp(static_cast<int>(lua_tonumber(L, 4)), -10, 10) : SpeechRate();
    job.volume = lua_isnumber(L, 5) ? Clamp(static_cast<int>(lua_tonumber(L, 5)), 0, 100) : SpeechVolume();
    EnsureVoices();
    SStrCopy(job.voice, VoiceName(static_cast<int>(lua_tonumber(L, 1))), sizeof(job.voice));
#if !defined(_WIN32)
    SanitizeSpeech(job.text, sizeof(job.text), lua_tostring(L, 2));
    bool spd = false;
    if (!job.text[0] || !SpeechProgram(spd)) FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, job.id, job.dest, "failed");
    else EnqueueSpeech(job);
#else
    FireSpeech(SCRIPT_EVENT_VOICE_CHAT_TTS_PLAYBACK_FAILED, job.id, job.dest, "unavailable");
#endif
    lua_pushnumber(L, job.id);
    return 1;
}

static int32_t Script_StopSpeakingText(lua_State*) {
#if !defined(_WIN32)
    s_speechCount = 0;
    if (s_liveOn) {
        kill(s_live.pid, SIGTERM);
        int status = 0;
        waitpid(s_live.pid, &status, WNOHANG);
    }
#endif
    return 0;
}

static int32_t Script_GetSpeechRate(lua_State* L) { lua_pushnumber(L, SpeechRate()); return 1; }
static int32_t Script_GetSpeechVolume(lua_State* L) { lua_pushnumber(L, SpeechVolume()); return 1; }
static int32_t Script_GetSpeechVoiceID(lua_State* L) { lua_pushnumber(L, SpeechVoice()); return 1; }

static int32_t Script_GetVoiceOptionName(lua_State* L) {
    EnsureVoices();
    lua_pushstring(L, VoiceName(SpeechVoice()));
    return 1;
}

static int32_t Script_SetSpeechRate(lua_State* L) {
    if (lua_isnumber(L, 1)) SetCVarInt("ttsSpeed", Clamp(static_cast<int>(lua_tonumber(L, 1)), -10, 10));
    return 0;
}

static int32_t Script_SetSpeechVolume(lua_State* L) {
    if (lua_isnumber(L, 1)) SetCVarInt("ttsVolume", Clamp(static_cast<int>(lua_tonumber(L, 1)), 0, 100));
    return 0;
}

static int32_t Script_SetVoiceOption(lua_State* L) {
    EnsureVoices();
    if (!lua_isnumber(L, 1)) return 0;
    int id = static_cast<int>(lua_tonumber(L, 1));
    if (id >= 1 && id <= s_voiceCount) SetCVarInt("ttsVoice", id);
    return 0;
}

static int32_t Script_SetVoiceOptionByName(lua_State* L) {
    EnsureVoices();
    if (!lua_isstring(L, 1)) return 0;
    const char* name = lua_tostring(L, 1);
    for (int i = 0; i < s_voiceCount; i++) {
        if (Exact(s_voices[i].name, name)) {
            SetCVarInt("ttsVoice", s_voices[i].id);
            break;
        }
    }
    return 0;
}

static int32_t Script_SetDefaultSettings(lua_State*) {
    SetCVarInt("ttsVoice", 1);
    SetCVarInt("ttsSpeed", 0);
    SetCVarInt("ttsVolume", 100);
    return 0;
}

static int32_t Script_RefreshVoices(lua_State*) {
    Voice next[32];
    int count = 0;
    ReadVoices(next, count);
    bool changed = count != s_voiceCount;
    for (int i = 0; !changed && i < count; i++) changed = !Exact(next[i].name, s_voices[i].name);
    memcpy(s_voices, next, sizeof(Voice) * static_cast<size_t>(count));
    s_voiceCount = count;
    if (changed && FrameScript_GetContext()) FrameScript_SignalEvent(SCRIPT_EVENT_VOICE_CHAT_TTS_VOICES_UPDATE, nullptr);
    return 0;
}

static void ApplyStance(int32_t spellId) {
    if (!CVarInt("enableStancePatch", 0)) return;
    static const struct { int32_t spell; uint8_t form; } kForms[] = {
        {768, 1}, {783, 3}, {1066, 4}, {5487, 5}, {9634, 8}, {33891, 2},
        {2457, 0x11}, {71, 0x12}, {2458, 0x13},
    };
    uint8_t form = 0;
    bool found = false;
    for (const auto& entry : kForms) {
        if (entry.spell == spellId) { form = entry.form; found = true; break; }
    }
    if (!found) return;
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (player && player->m_unit) player->m_unit->m_unitFieldBytes2[3] = form;
}

static bool GroundPoint(int kind, C3Vector& out, WOWGUID& guid) {
    guid = WOWGUID();
    if (kind == 2) {
        CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
        if (!player) return false;
        out = player->GetPosition();
        return true;
    }
    CGWorldFrame* world = CGWorldFrame::s_currentWorldFrame;
    if (!world) return false;
    HITTESTRESULT hit;
    hit.m_guid = WOWGUID();
    hit.m_point = C3Vector(0.0f, 0.0f, 0.0f);
    hit.m_distance = 0.0f;
    hit.m_segStart = C3Vector(0.0f, 0.0f, 0.0f);
    hit.m_segEnd = C3Vector(0.0f, 0.0f, 0.0f);
    if (!world->HitTestCursor(&hit)) return false;
    out = hit.m_point;
    uint32_t high = static_cast<uint32_t>(hit.m_guid.m_guid >> 32);
    if ((high & 0xF0000000) == 0x10000000 && (high & 0xFF00000) == 0xFC00000) guid = hit.m_guid;
    return true;
}

void AwesomeWotlkNoteGroundTarget(const char* kind) {
    if (Exact(kind, "playerlocation")) s_ground = 2;
    else if (Exact(kind, "cursor")) s_ground = 1;
    s_groundAge = 0;
}

void AwesomeWotlkAfterCast(int32_t spellId, bool succeeded) {
    int kind = s_ground;
    s_ground = 0;
    s_groundAge = 0;
    if (kind) {
        C3Vector point;
        WOWGUID guid;
        if (GroundPoint(kind, point, guid)) {
            CTerrainClickEvent evt;
            evt.m_guid = guid;
            evt.m_point = point;
            evt.m_button = MOUSE_BUTTON_LEFT;
            Spell_C_HandleTerrainClick(evt);
        }
    }
    if (succeeded) ApplyStance(spellId);
}

static void ApplyShowPlayer() {
    CVar* cvar = CVar::Lookup("showPlayer");
    if (!cvar) return;
    int value = cvar->m_intValue ? 1 : 0;
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (!player) return;
    if (!value) player->m_alpha = 0;
    else if (s_showApplied == 0) player->m_alpha = 255;
    s_showApplied = value;
}

static bool ShowPlayerCallback(CVar*, const char*, const char* value, void*) {
    int shown = value && value[0] != '0';
    if (CGPlayer_C* player = ClntObjMgrGetActivePlayerObj()) player->m_alpha = shown ? 255 : 0;
    s_showApplied = shown;
    return true;
}

static void AddCVar(const char* name, const char* value, const char* help, bool (*callback)(CVar*, const char*, const char*, void*) = nullptr) {
    CVar::Register(name, help, 0x10, value, callback, CATEGORY_GAME, false, nullptr, false);
}

static void SetTableFunction(lua_State* L, const char* table, const char* name, lua_CFunction fn) {
    lua_getfield(L, LUA_GLOBALSINDEX, table);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_GLOBALSINDEX, table);
    }
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

static void RegisterSlash() {
    if (s_slash) return;
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_GLOBALSINDEX, "SlashCmdList");
    if (!lua_istable(L, -1)) {
        lua_settop(L, top);
        return;
    }
    lua_pushcfunction(L, Script_InteractSlash);
    lua_setfield(L, -2, "INTERACTCMD");
    lua_pushstring(L, "/interact");
    lua_setfield(L, LUA_GLOBALSINDEX, "SLASH_INTERACTCMD1");
    lua_settop(L, top);
    s_slash = true;
}

void AwesomeWotlkService() {
    if (s_ground && ++s_groundAge > 1) s_ground = 0;
    RegisterSlash();
    RunInteract();
    ServiceSpeech();
    ApplyShowPlayer();
}

void AwesomeWotlkRegister() {
    if (s_registered) return;
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    s_registered = true;
    AddCVar("nameplateDistance", "41", "Nameplate distance in yards");
    AddCVar("nameplateStacking", "0", "Stack nameplates that overlap");
    AddCVar("nameplateXSpace", "130", "Horizontal nameplate spacing in pixels");
    AddCVar("nameplateYSpace", "20", "Vertical nameplate spacing in pixels");
    AddCVar("nameplateUpperBorder", "30", "Top inset for stacked nameplates in pixels");
    AddCVar("nameplateOriginPos", "20", "Extra nameplate origin offset in pixels");
    AddCVar("nameplateSpeedRaise", "1", "Stored nameplate raise speed");
    AddCVar("nameplateSpeedReset", "1", "Stored nameplate reset speed");
    AddCVar("nameplateSpeedLower", "1", "Stored nameplate lower speed");
    AddCVar("nameplateFriendlyHitboxHeight", "0", "Friendly nameplate height in pixels");
    AddCVar("nameplateFriendlyHitboxWidth", "0", "Friendly nameplate width in pixels");
    AddCVar("nameplateHitboxHeight", "0", "Nameplate height in pixels");
    AddCVar("nameplateHitboxWidth", "0", "Nameplate width in pixels");
    AddCVar("nameplateStackFriendly", "1", "Include friendly nameplates in stacking");
    AddCVar("nameplateStackFriendlyMode", "1", "0 reaction, 1 health bar color");
    AddCVar("nameplateMaxRaiseDistance", "200", "Maximum stacked raise in pixels");
    AddCVar("nameplateExtendWorldFrameHeight", "0", "Stored world-frame extension");
    AddCVar("nameplateUpperBorderOnlyBoss", "0", "Apply the upper border to bosses only");
    AddCVar("ttsVoice", "1", "Text to speech voice");
    AddCVar("ttsSpeed", "0", "Text to speech rate, -10 to 10");
    AddCVar("ttsVolume", "100", "Text to speech volume, 0 to 100");
    AddCVar("cameraIndirectVisibility", "0", "Let the camera pass through world objects");
    AddCVar("cameraIndirectAlpha", "0.6", "Stored camera fade alpha");
    AddCVar("interactionMode", "1", "1 keeps interact in a facing cone");
    AddCVar("interactionAngle", "60", "Interact cone angle in degrees");
    AddCVar("MSDFMode", "1", "Stored MSDF mode");
    AddCVar("cameraFov", "0", "Camera field of view, 0 retail, 1 to 200 wide");
    AddCVar("showPlayer", "1", "Show the player model", ShowPlayerCallback);
    AddCVar("enableStancePatch", "0", "Write the stance form byte after a form spell");

    SetTableFunction(L, "C_NamePlate", "GetNamePlates", Script_GetNamePlates);
    SetTableFunction(L, "C_NamePlate", "GetNamePlateForUnit", Script_GetNamePlateForUnit);
    SetTableFunction(L, "C_NamePlate", "GetNamePlateByGUID", Script_GetNamePlateByGUID);
    SetTableFunction(L, "C_NamePlate", "GetNamePlateTokenByGUID", Script_GetNamePlateTokenByGUID);
    SetTableFunction(L, "C_VoiceChat", "GetTtsVoices", Script_GetTtsVoices);
    SetTableFunction(L, "C_VoiceChat", "GetRemoteTtsVoices", Script_GetTtsVoices);
    SetTableFunction(L, "C_VoiceChat", "SpeakText", Script_SpeakText);
    SetTableFunction(L, "C_VoiceChat", "StopSpeakingText", Script_StopSpeakingText);
    SetTableFunction(L, "C_TTSSettings", "GetSpeechRate", Script_GetSpeechRate);
    SetTableFunction(L, "C_TTSSettings", "GetSpeechVolume", Script_GetSpeechVolume);
    SetTableFunction(L, "C_TTSSettings", "GetSpeechVoiceID", Script_GetSpeechVoiceID);
    SetTableFunction(L, "C_TTSSettings", "GetVoiceOptionName", Script_GetVoiceOptionName);
    SetTableFunction(L, "C_TTSSettings", "SetDefaultSettings", Script_SetDefaultSettings);
    SetTableFunction(L, "C_TTSSettings", "SetSpeechRate", Script_SetSpeechRate);
    SetTableFunction(L, "C_TTSSettings", "SetSpeechVolume", Script_SetSpeechVolume);
    SetTableFunction(L, "C_TTSSettings", "SetVoiceOption", Script_SetVoiceOption);
    SetTableFunction(L, "C_TTSSettings", "SetVoiceOptionByName", Script_SetVoiceOptionByName);
    SetTableFunction(L, "C_TTSSettings", "RefreshVoices", Script_RefreshVoices);

    FrameScript_RegisterFunction("UnitIsControlled", Script_UnitIsControlled);
    FrameScript_RegisterFunction("UnitIsDisarmed", Script_UnitIsDisarmed);
    FrameScript_RegisterFunction("UnitIsSilenced", Script_UnitIsSilenced);
    FrameScript_RegisterFunction("UnitOccupations", Script_UnitOccupations);
    FrameScript_RegisterFunction("UnitOwner", Script_UnitOwner);
    FrameScript_RegisterFunction("UnitTokenFromGUID", Script_UnitTokenFromGUID);
    FrameScript_RegisterFunction("GetInventoryItemTransmog", Script_GetInventoryItemTransmog);
    FrameScript_RegisterFunction("GetSpellBaseCooldown", Script_GetSpellBaseCooldown);
    FrameScript_RegisterFunction("GetItemInfoInstant", Script_GetItemInfoInstant);
    FrameScript_RegisterFunction("FlashWindow", Script_FlashWindow);
    FrameScript_RegisterFunction("IsWindowFocused", Script_IsWindowFocused);
    FrameScript_RegisterFunction("FocusWindow", Script_FocusWindow);
    FrameScript_RegisterFunction("CopyToClipboard", Script_CopyToClipboard);
    FrameScript_RegisterFunction("QueueInteract", Script_QueueInteract);
}

void AwesomeWotlkUnregister() {
    if (!s_registered) return;
    s_registered = false;
    s_slash = false;
    const char* names[] = {
        "UnitIsControlled", "UnitIsDisarmed", "UnitIsSilenced", "UnitOccupations", "UnitOwner", "UnitTokenFromGUID",
        "GetInventoryItemTransmog", "GetSpellBaseCooldown", "GetItemInfoInstant",
        "FlashWindow", "IsWindowFocused", "FocusWindow", "CopyToClipboard", "QueueInteract",
    };
    for (const char* name : names) FrameScript_UnregisterFunction(name);
    lua_State* L = FrameScript_GetContext();
    if (!L) return;
    lua_pushnil(L);
    lua_setfield(L, LUA_GLOBALSINDEX, "C_NamePlate");
    lua_pushnil(L);
    lua_setfield(L, LUA_GLOBALSINDEX, "C_VoiceChat");
    lua_pushnil(L);
    lua_setfield(L, LUA_GLOBALSINDEX, "C_TTSSettings");
    lua_pushnil(L);
    lua_setfield(L, LUA_GLOBALSINDEX, "SLASH_INTERACTCMD1");
}
