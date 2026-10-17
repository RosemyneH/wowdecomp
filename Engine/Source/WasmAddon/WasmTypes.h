#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace WasmAddon {

inline constexpr uint32_t kApiVersionMin = 1;
inline constexpr uint32_t kApiVersionMax = 2;
inline constexpr uint32_t kMaxManifestBytes = 64 * 1024;
inline constexpr uint32_t kMaxModuleBytes = 16 * 1024 * 1024;
inline constexpr uint32_t kMaxPackageBytes = 32 * 1024 * 1024;
inline constexpr uint64_t kHostMaxMemoryBytes = 256ull * 1024 * 1024;
inline constexpr uint64_t kDefaultFuelPerCallback = 20'000'000;
inline constexpr uint64_t kHostMaxFuelPerCallback = 50'000'000;
inline constexpr uint32_t kMaxEventsPerAddon = 128;
inline constexpr uint32_t kMaxUiHandlesPerAddon = 256;
inline constexpr uint32_t kMaxHostCallsPerCallback = 10000;

enum class AddonState { Discovered, Validated, Loaded, Running, Suspended, Failed, Unloaded };
const char* ToString(AddonState state);

enum HostError : int32_t {
    Ok = 0,
    ErrGeneric = -1,
    ErrPermissionDenied = -2,
    ErrInvalidMemory = -3,
    ErrInvalidHandle = -4,
    ErrInvalidArgument = -5,
    ErrLimitExceeded = -6,
    ErrBufferTooSmall = -7,
    ErrUnavailable = -8,
    ErrUnknownEvent = -9,
    ErrNotFound = -10,
};

enum Permission : uint32_t {
    PermNone = 0,
    PermInventoryRead = 1u << 0,
    PermUnitsRead = 1u << 1,
    PermUiCreate = 1u << 2,
    PermChatPrint = 1u << 3,
    PermChatRead = 1u << 4,
    PermChatSend = 1u << 5,
    PermAuctionRead = 1u << 6,
    PermAuctionWrite = 1u << 7,
    PermStorageRead = 1u << 8,
    PermStorageWrite = 1u << 9,
    PermNetworkAccess = 1u << 10,
    PermFilesystemAccess = 1u << 11,
    PermProcessExecute = 1u << 12,
    PermProfilingRead = 1u << 13,
    PermCombatRead = 1u << 14,
    PermLuaFull = 1u << 15,
    PermAwesome = 1u << 16,
    PermSurface = 1u << 17,
    PermHook = 1u << 18,
};
const char* PermissionName(Permission perm);
bool PermissionFromName(const std::string& name, Permission& out);
bool IsSensitive(Permission perm);
std::vector<Permission> AllPermissions();

inline constexpr uint32_t kMaxStorageKeyBytes = 128;
inline constexpr uint32_t kMaxStorageValueBytes = 64 * 1024;
inline constexpr uint32_t kMaxStorageKeysPerScope = 1024;
inline constexpr uint64_t kDefaultStorageQuotaBytes = 1024 * 1024;
inline constexpr uint64_t kHostMaxStorageQuotaBytes = 4 * 1024 * 1024;
inline constexpr uint64_t kMaxStorageBytesPerCallback = 4 * 1024 * 1024;

struct Limits {
    uint64_t memoryBytes = 32ull * 1024 * 1024;
    uint64_t fuelPerCallback = kDefaultFuelPerCallback;
    uint32_t maxUiHandles = kMaxUiHandlesPerAddon;
    uint32_t maxEvents = kMaxEventsPerAddon;
    uint64_t storageQuotaBytes = kDefaultStorageQuotaBytes;
};

struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string repository;
    std::string moduleFile = "addon.wasm";
    uint32_t apiVersion = 0;
    uint32_t declaredPermissions = 0;
    Limits requestedLimits;
};

struct InventoryItem {
    uint32_t bag = 0;
    uint32_t slot = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
};

// ʕ ◕ᴥ◕ ʔ✿ Fixed-size snapshot records; no names, GUIDs or positions ever leave the host ✿ ʕ ◕ᴥ◕ ʔ
struct UnitRecord {
    uint32_t tokenIndex = 0;
    uint32_t flags = 0; // 1 exists, 2 dead, 4 isPlayer, 8 isSelf
    uint32_t level = 0;
    uint32_t classId = 0;
    uint32_t raceId = 0;
    uint32_t health = 0;
    uint32_t healthMax = 0;
    uint32_t powerType = 0;
    uint32_t power = 0;
    uint32_t powerMax = 0;
};

struct AuctionRecord {
    uint32_t index = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint32_t minBid = 0;
    uint32_t minIncrement = 0;
    uint32_t buyout = 0;
    uint32_t bid = 0;
    uint32_t timeLeft = 0;
    uint32_t quality = 0;
    uint32_t flags = 0; // 1 highBidderIsMe, 2 ownerIsMe, 4 sold
};

struct AuctionSnapshot {
    uint32_t total = 0;
    uint32_t flags = 0; // 1 auction house query succeeded
    std::vector<AuctionRecord> records;
};

struct ActivityEntry {
    uint64_t timestampMs = 0;
    std::string addonId;
    std::string kind;
    std::string detail;
};

inline constexpr uint32_t kMaxEventArgs = 32;
inline constexpr uint32_t kMaxEventString = 1024;
inline constexpr uint32_t kMaxEventPayload = 4096;

// ʕ ◕ᴥ◕ ʔ✿ WEV1 payload: 16-byte header, name, then tag-typed args (0 nil, 1 bool, 2 f64, 3 string, 4 truncated string, 5 redacted) ✿ ʕ ◕ᴥ◕ ʔ
class EventPayloadWriter {
public:
    EventPayloadWriter(const std::string& name, uint32_t index) {
        m_bytes.resize(16, 0);
        m_bytes[0] = 'W'; m_bytes[1] = 'E'; m_bytes[2] = 'V'; m_bytes[3] = '1';
        PutU16(4, 2);
        PutU32(12, index);
        PutU16At(m_bytes.size(), (uint16_t)name.size());
        m_bytes.insert(m_bytes.end(), name.begin(), name.end());
    }
    void Nil() { Add(0, {}); }
    void Bool(bool v) { Add(1, {(uint8_t)v}); }
    void Number(double v) { uint8_t b[8]; std::memcpy(b, &v, 8); Add(2, std::vector<uint8_t>(b, b + 8)); }
    void Redacted() { Add(5, {}); }
    void String(const char* data, size_t len) {
        if (len <= kMaxEventString) {
            std::vector<uint8_t> body;
            AppendU32(body, (uint32_t)len);
            body.insert(body.end(), data, data + len);
            Add(3, body);
        } else {
            std::vector<uint8_t> body;
            AppendU32(body, (uint32_t)len);
            AppendU32(body, kMaxEventString);
            body.insert(body.end(), data, data + kMaxEventString);
            Add(4, body);
        }
    }
    std::vector<uint8_t> Finish() {
        PutU16(6, (uint16_t)m_argc);
        PutU32(8, (uint32_t)m_bytes.size());
        return m_bytes;
    }

private:
    static void AppendU32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (i * 8))); }
    void PutU16(size_t at, uint16_t v) { m_bytes[at] = (uint8_t)v; m_bytes[at + 1] = (uint8_t)(v >> 8); }
    void PutU32(size_t at, uint32_t v) { for (int i = 0; i < 4; i++) m_bytes[at + i] = (uint8_t)(v >> (i * 8)); }
    void PutU16At(size_t at, uint16_t v) { m_bytes.resize(at + 2); PutU16(at, v); }
    void Add(uint8_t tag, const std::vector<uint8_t>& body) {
        if (m_argc >= kMaxEventArgs) return;
        if (m_bytes.size() + 1 + body.size() > kMaxEventPayload && tag != 5) { Add(5, {}); return; }
        m_bytes.push_back(tag);
        m_bytes.insert(m_bytes.end(), body.begin(), body.end());
        m_argc++;
    }
    std::vector<uint8_t> m_bytes;
    uint32_t m_argc = 0;
};

struct AddonStats {
    uint64_t hostCalls = 0;
    uint64_t hostCallsByFn[40] = {};
    uint64_t eventsReceived = 0;
    uint64_t traps = 0;
    uint64_t denials = 0;
    uint64_t fuelUsed = 0;
    uint64_t memoryBytes = 0;
    uint64_t peakMemoryBytes = 0;
    uint64_t callbacks = 0;
    uint64_t callbackTimeUs = 0;
    uint64_t maxCallbackTimeUs = 0;
};

struct InspectorInfo {
    Manifest manifest;
    AddonState state = AddonState::Discovered;
    std::string directory;
    std::string sha256;
    bool signaturePresent = false;
    uint32_t grantedPermissions = 0;
    uint32_t deniedPermissions = 0;
    std::vector<std::string> importedFunctions;
    std::vector<std::string> prohibitedImports;
    std::vector<std::string> subscriptions;
    uint32_t activeUiHandles = 0;
    uint64_t storageBytes[2] = {};
    uint32_t storageKeys[2] = {};
    AddonStats stats;
    std::string lastError;
};

enum class ConfirmAction : uint32_t { ChatSend = 0, AuctionBid = 1 };
enum class ConfirmStatus : uint32_t { Accepted = 0, Denied = 1, Expired = 2, Failed = 3 };

struct AuctionIdentity {
    uint32_t auctionId = 0;
    uint32_t itemId = 0;
    uint32_t count = 0;
    uint32_t buyout = 0;
    uint32_t minBid = 0;
};

struct ConfirmRequest {
    uint32_t id = 0;
    size_t addonIndex = 0;
    ConfirmAction action = ConfirmAction::ChatSend;
    int32_t chatType = 0;
    std::string target;
    std::string message;
    int32_t listKind = 0;
    uint32_t auctionIndex = 0;
    uint32_t bid = 0;
    AuctionIdentity identity;
    uint64_t createdMs = 0;
    uint64_t shownMs = 0;
};

struct ConfirmExecResult {
    bool ok = false;
    int32_t detail = 0;
};

// ʕ •ᴥ•ʔ✿ ui_style blobs are little-endian. Sizes are UI pixels. Points: 0 TL, 1 T, 2 TR, 3 L, 4 C, 5 R, 6 BL, 7 B, 8 BR ✿ ʕ •ᴥ•ʔ
enum class UiOp : int32_t {
    SetSize = 1, SetPoint = 2, SetColor = 3, SetShown = 4, SetValue = 5, SetJustify = 6,
    SetFontSize = 7, SetBackdrop = 8, SetTextColor = 9, SetLevel = 10, SetStrata = 11,
    SetAlpha = 12, SetMouse = 13, SetTexture = 14,
};

enum class UiKind : uint8_t { None = 0, Frame = 1, FontString = 2, Texture = 3, StatusBar = 4, Button = 5, EditBox = 6, ScrollFrame = 7, Canvas = 8, Model = 9, Surface = 10 };

class IWasmHostServices {
public:
    virtual ~IWasmHostServices() = default;
    virtual bool ChatPrint(const std::string& addonId, const std::string& text) = 0;
    virtual bool GetInventorySnapshot(std::vector<InventoryItem>& out) = 0;
    virtual bool GetUnitSnapshot(const std::vector<std::string>& tokens, std::vector<UnitRecord>& out) { (void)tokens; (void)out; return false; }
    virtual bool GetAuctionSnapshot(int32_t kind, AuctionSnapshot& out) { (void)kind; (void)out; return false; }
    virtual uint64_t UiCreate(const std::string& addonId, const std::string& kind) { (void)addonId; (void)kind; return 0; }
    virtual uint64_t UiSpawn(const std::string& addonId, uint64_t parentHost, const std::string& kind) { (void)addonId; (void)parentHost; (void)kind; return 0; }
    virtual bool UiStyle(uint64_t hostHandle, int32_t op, const uint8_t* data, uint32_t len) { (void)hostHandle; (void)op; (void)data; (void)len; return false; }
    virtual bool UiSetText(uint64_t hostHandle, const std::string& text) { (void)hostHandle; (void)text; return false; }
    virtual bool UiBind(uint64_t hostHandle, int32_t guestHandle, const std::string& event) { (void)hostHandle; (void)guestHandle; (void)event; return false; }
    virtual bool UiGetText(uint64_t hostHandle, std::string& out) { (void)hostHandle; (void)out; return false; }
    virtual bool GetUnitNames(const std::vector<std::string>& tokens, std::vector<std::pair<uint32_t, std::string>>& out) { (void)tokens; (void)out; return false; }
    // ʕ •ᴥ•ʔ✿ Runs in the client Lua state, so the guest can call any 3.3.5 API the server’s client exposes ✿ ʕ •ᴥ•ʔ
    virtual int ExecuteLua(const std::string& addonId, const std::string& chunkName, const std::string& source, std::string& error) { (void)addonId; (void)chunkName; (void)source; (void)error; return ErrUnavailable; }
    virtual int LoadLuaPath(const std::string& addonId, const std::string& addonRoot, const std::string& absolutePath, std::string& error) { (void)addonId; (void)addonRoot; (void)absolutePath; (void)error; return ErrUnavailable; }
    virtual int LoadNamedAddon(const std::string& name, std::string& error) { (void)name; (void)error; return ErrUnavailable; }
    virtual int CallLua(const std::string& addonId, const uint8_t* req, uint32_t reqLen, std::vector<uint8_t>& out, std::string& error) { (void)addonId; (void)req; (void)reqLen; (void)out; (void)error; return ErrUnavailable; }
    virtual int BindLua(const std::string& addonId, int32_t handle, const std::string& script, std::string& error) { (void)addonId; (void)handle; (void)script; (void)error; return ErrUnavailable; }
    virtual int AwesomeCall(const std::string& addonId, const uint8_t* req, uint32_t reqLen, std::vector<uint8_t>& out, std::string& error) { (void)addonId; (void)req; (void)reqLen; (void)out; (void)error; return ErrUnavailable; }
    virtual uint64_t SurfaceOpen(const std::string& addonId, const std::string& spec, int32_t width, int32_t height, std::string& error) { (void)addonId; (void)spec; (void)width; (void)height; (void)error; return 0; }
    virtual bool CanvasWrite(uint64_t hostHandle, int32_t width, int32_t height, const uint8_t* rgba, uint32_t len) { (void)hostHandle; (void)width; (void)height; (void)rgba; (void)len; return false; }
    virtual bool ModelSet(uint64_t hostHandle, const std::string& spec) { (void)hostHandle; (void)spec; return false; }
    virtual bool HookBind(const std::string& addonId, const std::string& name) { (void)addonId; (void)name; return false; }
    virtual void ReleaseLua(const std::string& addonId) { (void)addonId; }
    virtual void UiDestroy(uint64_t hostHandle) { (void)hostHandle; }
    virtual void Log(const std::string& message) { (void)message; }
    virtual void ConfirmShow(const ConfirmRequest& request) { (void)request; }
    virtual void ConfirmHide(uint32_t requestId) { (void)requestId; }
    virtual ConfirmExecResult ExecuteChatSend(const ConfirmRequest& request) { (void)request; return {}; }
    virtual ConfirmExecResult ExecuteAuctionBid(const ConfirmRequest& request) { (void)request; return {}; }
    virtual bool ResolveAuctionIdentity(int32_t listKind, uint32_t index, AuctionIdentity& out) { (void)listKind; (void)index; (void)out; return false; }
};

enum class HostFn : uint32_t {
    ApiVersion = 0, Ping, ChatPrint, EventSubscribe, InventorySnapshot, UiCreate, UiSetText, PerfQuery,
    StorageGet, StorageLen, StorageKeys, StorageUsage, StorageSet, StorageDelete, UnitSnapshot, AuctionSnapshot, ChatSendRequest, AuctionBidRequest, UiSpawn, UiStyle, UiBind, UiGetText, UnitNames,     LuaExec, LuaLoad, LuaLoadAddon, LuaCall, LuaBind, AwesomeCall, SurfaceOpen, CanvasWrite, ModelSet, HookBind, Count
};
static_assert((uint32_t)HostFn::Count <= 40, "hostCallsByFn array too small");


} // namespace WasmAddon
