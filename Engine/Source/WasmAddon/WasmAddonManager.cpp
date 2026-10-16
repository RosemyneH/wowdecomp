#include "Engine/Source/WasmAddon/WasmAddonManager.h"
#include "Engine/Source/WasmAddon/WasmStorageFormat.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace WasmAddon {
namespace {
uint64_t NowMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool ReadFileLimited(const fs::path& path, uintmax_t maxBytes, std::vector<uint8_t>& out) {
    std::error_code ec;
    uintmax_t size = fs::file_size(path, ec);
    if (ec || size > maxBytes) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.resize((size_t)size);
    return size == 0 || (bool)in.read(reinterpret_cast<char*>(out.data()), (std::streamsize)size);
}
void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); out.push_back((uint8_t)(v >> 16)); out.push_back((uint8_t)(v >> 24));
}
} // namespace

bool WasmAddonManager::Initialize(const std::string& rootDir, IWasmHostServices* host, const std::string& permissionFile) {
    // ʕ ●ᴥ●ʔ✿ Re-initialising must not strand live instances on a destroyed engine ✿ ʕ ●ᴥ●ʔ
    if (m_initialized) Shutdown();
    m_rootDir = rootDir;
    m_permissionFile = permissionFile.empty() ? (fs::path(rootDir) / "wasm-permissions.cfg").string() : permissionFile;
    m_host = host;
    m_runtime = CreateRuntime();
    LoadPermissionStore();
    m_initialized = true;
    RecordActivity("", "runtime.init", RuntimeAvailable() ? "wasmtime" : "unavailable");
    if (m_host) m_host->Log(RuntimeAvailable() ? "[WasmAddon] runtime initialized" : "[WasmAddon] Wasmtime unavailable; addons disabled");
    return true;
}

void WasmAddonManager::Shutdown() {
    for (auto& addon : m_addons)
        if (addon->instance) UnloadAddon(addon->info.manifest.id);
    m_addons.clear();
    m_runtime.reset();
    m_initialized = false;
}

void WasmAddonManager::RecordActivity(const std::string& addonId, const std::string& kind, const std::string& detail) {
    m_activity.push_back({NowMs(), addonId, kind, detail});
    if (m_activity.size() > 256) m_activity.erase(m_activity.begin(), m_activity.begin() + 64);
}
void WasmAddonManager::RecordError(Addon& addon, const std::string& error) {
    addon.info.lastError = error;
    RecordActivity(addon.info.manifest.id, "error", error);
    if (m_host) m_host->Log("[WasmAddon] " + addon.info.manifest.id + ": " + error);
}
void WasmAddonManager::FailAddon(Addon& addon, const std::string& error) {
    CancelConfirmFor(IndexOf(addon));
    addon.info.state = error.find("fuel") != std::string::npos ? AddonState::Suspended : AddonState::Failed;
    addon.info.stats.traps++;
    RecordError(addon, error);
}

bool WasmAddonManager::ClearStorage(const std::string& addonId, int scope) {
    for (auto& addon : m_addons) {
        if (addon->info.manifest.id != addonId || scope < 0 || scope > 1) continue;
        addon->storage[scope].clear();
        addon->storageDirty[scope] = true;
        SaveStorage(*addon, scope);
        RecordActivity(addonId, "storage.clear", scope == 0 ? "account" : "character");
        return true;
    }
    return false;
}

void WasmAddonManager::SetStorageDirs(const std::string& accountDir, const std::string& characterDir) {
    m_storageAccountDir = accountDir;
    m_storageCharacterDir = characterDir;
}

static void PutU16(std::vector<uint8_t>& out, uint16_t v) { out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); }
static uint32_t GetU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void WasmAddonManager::LoadStorage(Addon& addon, int scope) {
    std::string dir = StorageDir(scope);
    if (dir.empty()) return;
    fs::path file = fs::path(dir) / (addon.info.manifest.id + ".kv");
    std::vector<uint8_t> bytes;
    if (ReadFileLimited(file, kHostMaxStorageQuotaBytes + 4096, bytes) && ParseStorageFile(bytes.data(), bytes.size(), scope, addon.storage[scope])) return;
    std::vector<uint8_t> backup;
    if (ReadFileLimited(fs::path(file.string() + ".bak"), kHostMaxStorageQuotaBytes + 4096, backup) && ParseStorageFile(backup.data(), backup.size(), scope, addon.storage[scope]))
        RecordActivity(addon.info.manifest.id, "storage.recovered", file.filename().string());
}

void WasmAddonManager::SaveStorage(Addon& addon, int scope) {
    if (!addon.storageDirty[scope]) return;
    addon.storageDirty[scope] = false;
    std::string dir = StorageDir(scope);
    if (dir.empty()) return;
    std::error_code ec;
    fs::path file = fs::path(dir) / (addon.info.manifest.id + ".kv");
    if (addon.storage[scope].empty()) { fs::remove(file, ec); fs::remove(file.string() + ".bak", ec); return; }
    fs::create_directories(dir, ec);
    std::vector<uint8_t> bytes = SerializeStorageFile(scope, addon.storage[scope]);
    fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        out.flush();
        if (!out) { fs::remove(tmp, ec); return; }
    }
    if (fs::exists(file)) fs::copy_file(file, file.string() + ".bak", fs::copy_options::overwrite_existing, ec);
    fs::rename(tmp, file, ec);
    if (ec) fs::remove(tmp, ec);
}

static bool IsValidUnitToken(const std::string& token) {
    static const char* kExact[] = {"player", "pet", "target", "focus", "mouseover", "targettarget"};
    for (auto e : kExact) if (token == e) return true;
    auto ranged = [&](const char* prefix, int max) {
        size_t len = std::strlen(prefix);
        if (token.compare(0, len, prefix) != 0 || token.size() == len) return false;
        int n = 0;
        for (size_t i = len; i < token.size(); i++) {
            if (token[i] < '0' || token[i] > '9') return false;
            n = n * 10 + (token[i] - '0');
        }
        return n >= 1 && n <= max;
    };
    return ranged("party", 4) || ranged("partypet", 4) || ranged("raid", 40) || ranged("raidpet", 40) || ranged("boss", 5) || ranged("arena", 5);
}

static bool IsEventToken(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

bool WasmAddonManager::IsKnownEvent(const std::string& name) {
    if (!IsEventToken(name)) return false;
    static const char* kEvents[] = {"BAG_UPDATE","BAG_UPDATE_DELAYED","PLAYER_LOGIN","PLAYER_LOGOUT",
        "PLAYER_ENTERING_WORLD","ADDON_LOADED","CHAT_MSG_SYSTEM","UNIT_HEALTH","AUCTION_HOUSE_SHOW","WOW_ON_LOAD_TEST",
        "COMBAT_LOG_EVENT","COMBAT_LOG_EVENT_UNFILTERED","AUCTION_ITEM_LIST_UPDATE","AUCTION_HOUSE_CLOSED","WASM_CONFIRM_RESULT",
        "WASM_UI","WASM_SLASH"};
    for (auto e : kEvents) if (name == e) return true;
    auto starts = [&](const char* prefix) {
        size_t n = std::strlen(prefix);
        return name.size() > n && name.compare(0, n, prefix) == 0;
    };
    return starts("UNIT_") || starts("CHAT_MSG_") || starts("PLAYER_");
}

std::vector<uint8_t> WasmAddonManager::SerializeInventory(const std::vector<InventoryItem>& items) {
    std::vector<uint8_t> out;
    PutU32(out, (uint32_t)items.size());
    for (auto& item : items) { PutU32(out, item.bag); PutU32(out, item.slot); PutU32(out, item.itemId); PutU32(out, item.count); }
    return out;
}

void WasmAddonManager::LoadPermissionStore() {
    m_permissionStoreLocked = false;
    std::vector<uint8_t> raw;
    if (ReadFileLimited(m_permissionFile, 4 * 1024 * 1024, raw)) {
        ParsePermissionStore(std::string(raw.begin(), raw.end()), m_permissionStore);
    } else {
        m_permissionStore.clear();
        // ʕ •ᴥ•ʔ✿ A store we couldn't read may hold explicit denies; never overwrite it ✿ ʕ •ᴥ•ʔ
        std::error_code ec;
        if (fs::exists(m_permissionFile, ec)) m_permissionStoreLocked = true;
    }
}

// ʕ ◕ᴥ◕ ʔ✿ Denies follow the addon id across module updates; grants stay hash-bound ✿ ʕ ◕ᴥ◕ ʔ
void WasmAddonManager::MigrateDenyEntries(const Manifest& manifest, const std::string& sha256) {
    bool changed = false;
    for (Permission perm : AllPermissions()) {
        if (!(manifest.declaredPermissions & perm)) continue;
        std::string current = PermissionKey(manifest.id, sha256, perm);
        if (m_permissionStore.count(current)) continue;
        std::string prefix = manifest.id + "|";
        std::string suffix = "|" + std::string(PermissionName(perm));
        for (auto& [key, allow] : m_permissionStore) {
            if (!allow && key.compare(0, prefix.size(), prefix) == 0 &&
                key.size() > suffix.size() && key.compare(key.size() - suffix.size(), std::string::npos, suffix) == 0) {
                m_permissionStore[current] = false;
                changed = true;
                break;
            }
        }
    }
    if (changed) SavePermissionStore();
}
void WasmAddonManager::SavePermissionStore() const {
    if (m_permissionStoreLocked) return;
    std::error_code ec;
    fs::path file(m_permissionFile);
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    // ʕ •ᴥ•ʔ✿ Write-then-rename so a crash cannot lose explicit denies ✿ ʕ •ᴥ•ʔ
    fs::path tmp = file.string() + ".tmp";
    bool written = false;
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << SerializePermissionStore(m_permissionStore);
        out.flush();
        written = (bool)out;
    }
    if (written) fs::rename(tmp, file, ec);
    if (ec || !written) fs::remove(tmp, ec);
}

uint64_t WasmAddonManager::Now() const { return m_nowFn ? m_nowFn() : NowMs(); }

size_t WasmAddonManager::IndexOf(const Addon& addon) const {
    for (size_t i = 0; i < m_addons.size(); i++)
        if (m_addons[i].get() == &addon) return i;
    return (size_t)-1;
}

void WasmAddonManager::ShowNextConfirm() {
    const ConfirmRequest* front = m_confirm.Front();
    if (front && m_host) m_host->ConfirmShow(*front);
}

void WasmAddonManager::CancelConfirmFor(size_t addonIndex) {
    uint32_t cancelledId = 0;
    if (m_confirm.CancelAddon(addonIndex, Now(), cancelledId) && m_host) {
        m_host->ConfirmHide(cancelledId);
        ShowNextConfirm();
    }
}

void WasmAddonManager::DeliverConfirmResult(const ConfirmQueue::Completed& completed, int32_t detail) {
    EventPayloadWriter writer("WASM_CONFIRM_RESULT", 0);
    writer.Number((double)completed.id);
    writer.Number((double)completed.status);
    writer.Number((double)completed.request.action);
    writer.Number((double)detail);
    DispatchEventTo(completed.addonIndex, "WASM_CONFIRM_RESULT", writer.Finish());
    std::string addonId = completed.addonIndex < m_addons.size() ? m_addons[completed.addonIndex]->info.manifest.id : "";
    RecordActivity(addonId, "confirm.result", std::to_string(completed.id) + " status=" + std::to_string((uint32_t)completed.status));
}

void WasmAddonManager::TickUi(uint64_t nowMs) {
    if (m_dispatching || m_callbackDepth > 0 || nowMs < m_nextUiTick) return;
    m_nextUiTick = nowMs + 200;
    for (auto& addonPtr : m_addons) {
        Addon& addon = *addonPtr;
        if (addon.info.state != AddonState::Running || !addon.instance || !addon.instance->HasExport("wow_on_update")) continue;
        int64_t ignored = 0;
        InvokeExport(addon, "wow_on_update", {}, ignored);
    }
}

void WasmAddonManager::TickConfirm() {
    const ConfirmRequest* before = m_confirm.Front();
    uint32_t frontId = before ? before->id : 0;
    for (auto& completed : m_confirm.Tick(Now())) {
        if (completed.id == frontId && m_host) m_host->ConfirmHide(completed.id);
        DeliverConfirmResult(completed, 0);
    }
    const ConfirmRequest* after = m_confirm.Front();
    if (after && after->id != frontId) ShowNextConfirm();
}

bool WasmAddonManager::ResolveConfirm(uint32_t requestId, bool accept) {
    const ConfirmRequest* front = m_confirm.Front();
    if (!front || front->id != requestId) return false;
    ConfirmQueue::Completed completed;
    if (!m_confirm.ResolveFront(accept, Now(), completed)) return false;
    if (m_host) m_host->ConfirmHide(requestId);
    if (completed.status == ConfirmStatus::Accepted && completed.addonIndex < m_addons.size()) {
        Permission needed = completed.request.action == ConfirmAction::ChatSend ? PermChatSend : PermAuctionWrite;
        if (!(m_addons[completed.addonIndex]->info.grantedPermissions & needed)) {
            completed.status = ConfirmStatus::Failed;
            DeliverConfirmResult(completed, 2);
            ShowNextConfirm();
            return true;
        }
        ConfirmExecResult result;
        if (completed.request.action == ConfirmAction::ChatSend) {
            result = m_host ? m_host->ExecuteChatSend(completed.request) : ConfirmExecResult{};
        } else {
            // ʕ ●ᴥ●ʔ✿ Re-resolve the auction at accept time; a re-sorted list must not redirect the bid ✿ ʕ ●ᴥ●ʔ
            AuctionIdentity now;
            bool same = m_host && m_host->ResolveAuctionIdentity(completed.request.listKind, completed.request.auctionIndex, now) &&
                now.auctionId == completed.request.identity.auctionId && now.itemId == completed.request.identity.itemId &&
                now.count == completed.request.identity.count && now.buyout == completed.request.identity.buyout &&
                now.minBid == completed.request.identity.minBid;
            result = same ? m_host->ExecuteAuctionBid(completed.request) : ConfirmExecResult{false, 1};
        }
        if (!result.ok) completed.status = ConfirmStatus::Failed;
        DeliverConfirmResult(completed, result.detail);
    } else if (completed.status == ConfirmStatus::Denied) {
        DeliverConfirmResult(completed, 0);
    }
    ShowNextConfirm();
    return true;
}

void WasmAddonManager::RearmConfirm() { m_confirm.Rearm(Now()); }

void WasmAddonManager::DispatchEventTo(size_t addonIndex, const std::string& name, const std::vector<uint8_t>& payload) {
    if (addonIndex >= m_addons.size()) return;
    Addon& addon = *m_addons[addonIndex];
    if (addon.info.state != AddonState::Running || !addon.instance || !addon.instance->HasExport("wow_on_event_v2")) return;
    addon.info.stats.eventsReceived++;
    int64_t allocPtr = 0, ignored = 0;
    if (!addon.instance->HasExport("wow_alloc") || !InvokeExport(addon, "wow_alloc", {(int64_t)payload.size()}, allocPtr) || allocPtr <= 0) return;
    if (!addon.instance->WriteMemory((uint32_t)allocPtr, payload.data(), (uint32_t)payload.size())) return;
    InvokeExport(addon, "wow_on_event_v2", {allocPtr, (int64_t)payload.size()}, ignored);
    if (addon.info.state == AddonState::Running && addon.instance->HasExport("wow_free"))
        InvokeExport(addon, "wow_free", {allocPtr, (int64_t)payload.size()}, ignored);
}

uint32_t WasmAddonManager::EffectiveGrants(const Manifest& manifest, const std::string& sha256) const {
    return WasmAddon::EffectiveGrants(m_permissionStore, manifest, sha256);
}

void WasmAddonManager::SetPermissionOverride(const std::string& addonId, Permission perm, bool granted) {
    for (auto& addon : m_addons) {
        if (addon->info.manifest.id != addonId) continue;
        if (!(addon->info.manifest.declaredPermissions & perm)) return;
        // ʕ •ᴥ•ʔ✿ One live entry per id+permission: stale hashes from old builds are pruned ✿ ʕ •ᴥ•ʔ
        {
            std::string prefix = addonId + "|";
            std::string suffix = "|" + std::string(PermissionName(perm));
            std::string current = PermissionKey(addonId, addon->info.sha256, perm);
            for (auto it = m_permissionStore.begin(); it != m_permissionStore.end();) {
                const std::string& key = it->first;
                if (key != current && key.compare(0, prefix.size(), prefix) == 0 &&
                    key.size() > suffix.size() && key.compare(key.size() - suffix.size(), std::string::npos, suffix) == 0)
                    it = m_permissionStore.erase(it);
                else
                    ++it;
            }
        }
        m_permissionStore[PermissionKey(addonId, addon->info.sha256, perm)] = granted;
        SavePermissionStore();
        addon->info.grantedPermissions = EffectiveGrants(addon->info.manifest, addon->info.sha256);
        addon->info.deniedPermissions = addon->info.manifest.declaredPermissions & ~addon->info.grantedPermissions;
        RecordActivity(addonId, granted ? "permission.grant" : "permission.revoke", PermissionName(perm));
        return;
    }
}
bool WasmAddonManager::GrantSensitive(const std::string& addonId, Permission perm) {
    for (auto& addon : m_addons)
        if (addon->info.manifest.id == addonId && (addon->info.manifest.declaredPermissions & perm)) {
            SetPermissionOverride(addonId, perm, true);
            return true;
        }
    return false;
}

WasmAddonManager::UiHandle* WasmAddonManager::FindUi(Addon& addon, int64_t guest) {
    if (guest <= 0 || guest > 0x7fffffff) return nullptr;
    uint32_t handle = (uint32_t)guest;
    auto it = addon.uiHandles.find(handle & 0xffff);
    if (it == addon.uiHandles.end() || it->second.generation != (handle >> 16)) return nullptr;
    return &it->second;
}

static bool CanParent(UiKind kind) {
    return kind == UiKind::Frame || kind == UiKind::StatusBar || kind == UiKind::Button
        || kind == UiKind::EditBox || kind == UiKind::ScrollFrame;
}

static bool IsUiBindEvent(const std::string& event) {
    static const char* kEvents[] = {"click", "enter", "leave", "escape", "enter_pressed", "text_changed"};
    for (auto e : kEvents) if (event == e) return true;
    return false;
}

// ʕ •ᴥ•ʔ✿ Texture paths stay inside Interface and cannot smuggle links or parent traversal ✿ ʕ •ᴥ•ʔ
static bool IsUiTexturePath(const std::string& path) {
    if (path.empty() || path.size() > 128) return false;
    if (path.compare(0, 10, "Interface\\") != 0 && path.compare(0, 10, "Interface/") != 0) return false;
    if (path.find("..") != std::string::npos || path.find('|') != std::string::npos) return false;
    for (unsigned char c : path)
        if (c < 0x20 || c > 0x7e) return false;
    return true;
}

static int32_t ReadI32(const uint8_t* p) { return (int32_t)GetU32(p); }

static bool InRange(int32_t v, int32_t lo, int32_t hi) { return v >= lo && v <= hi; }

bool WasmAddonManager::HasPermission(Addon& addon, Permission perm, HostFn) {
    if (addon.info.grantedPermissions & perm) return true;
    addon.info.stats.denials++;
    RecordActivity(addon.info.manifest.id, "permission.denied", PermissionName(perm));
    return false;
}

bool WasmAddonManager::InvokeExport(Addon& addon, const char* name, const std::vector<int64_t>& args, int64_t& result) {
    if (!addon.instance) return false;
    addon.hostCallsInCallback = 0;
    addon.storageBytesInCallback = 0;
    auto start = std::chrono::steady_clock::now();
    std::string error;
    m_callbackDepth++;
    bool ok = addon.instance->Call(name, args, result, error);
    m_callbackDepth--;
    uint64_t us = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    addon.info.stats.callbacks++;
    addon.info.stats.callbackTimeUs += us;
    addon.info.stats.maxCallbackTimeUs = std::max(addon.info.stats.maxCallbackTimeUs, us);
    addon.info.stats.fuelUsed += addon.instance->ConsumedFuel();
    addon.info.stats.memoryBytes = addon.instance->MemoryBytes();
    addon.info.stats.peakMemoryBytes = std::max(addon.info.stats.peakMemoryBytes, addon.info.stats.memoryBytes);
    if (!ok && error.compare(0, 5, "host ") != 0) FailAddon(addon, std::string(name) + ": " + error);
    if (!ok) addon.info.lastError = error;
    if (m_callbackDepth == 0) FlushQueuedEvents();
    return ok;
}

void WasmAddonManager::FlushQueuedEvents() {
    while (!m_queuedEvents.empty() && !m_dispatching) {
        QueuedEvent next = std::move(m_queuedEvents.front());
        m_queuedEvents.erase(m_queuedEvents.begin());
        DispatchEvent(next.name, next.payload);
    }
}

int64_t WasmAddonManager::HandleHostCall(Addon& addon, IWasmInstance& instance, HostFn fn, const std::vector<int64_t>& args) {
    addon.info.stats.hostCalls++;
    if ((uint32_t)fn < (uint32_t)HostFn::Count) addon.info.stats.hostCallsByFn[(uint32_t)fn]++;
    if (++addon.hostCallsInCallback > kMaxHostCallsPerCallback) return ErrLimitExceeded;
    auto readString = [&](size_t a, size_t b, std::string& out) {
        if (args.size() <= b || args[a] < 0 || args[b] < 0 || args[b] > 4096) return false;
        return instance.ReadMemory((uint32_t)args[a], (uint32_t)args[b], out);
    };
    switch (fn) {
        case HostFn::ApiVersion: return kApiVersionMax;
        case HostFn::Ping: return 1;
        case HostFn::ChatPrint: {
            if (!HasPermission(addon, PermChatPrint, fn)) return ErrPermissionDenied;
            std::string text;
            if (args.size() < 2 || args[0] < 0 || args[1] < 0) return ErrInvalidMemory;
            // ʕ ◕ᴥ◕ ʔ✿ Cap before copying, and strip WoW formatting codes so addons cannot spoof system lines ✿ ʕ ◕ᴥ◕ ʔ
            if (!instance.ReadMemory((uint32_t)args[0], (uint32_t)std::min<int64_t>(args[1], 512), text)) return ErrInvalidMemory;
            text = SanitizeDisplayText(text);
            RecordActivity(addon.info.manifest.id, "chat.print", text);
            return m_host && m_host->ChatPrint(addon.info.manifest.id, text) ? Ok : ErrUnavailable;
        }
        case HostFn::EventSubscribe: {
            std::string name;
            if (!readString(0, 1, name)) return ErrInvalidMemory;
            if (!IsKnownEvent(name)) return ErrUnknownEvent;
            if (addon.subscriptionsSet.size() >= addon.limits.maxEvents && !addon.subscriptionsSet.count(name)) return ErrLimitExceeded;
            addon.subscriptionsSet.insert(name);
            return Ok;
        }
        case HostFn::InventorySnapshot: {
            if (!HasPermission(addon, PermInventoryRead, fn)) return ErrPermissionDenied;
            if (!m_host || args.size() < 2 || args[0] < 0 || args[1] < 0) return ErrInvalidArgument;
            std::vector<InventoryItem> items;
            if (!m_host->GetInventorySnapshot(items)) return ErrUnavailable;
            std::vector<uint8_t> bytes = SerializeInventory(items);
            if ((uint64_t)args[1] < bytes.size()) return ErrBufferTooSmall;
            if (!instance.WriteMemory((uint32_t)args[0], bytes.data(), (uint32_t)bytes.size())) return ErrInvalidMemory;
            RecordActivity(addon.info.manifest.id, "inventory.read", std::to_string(items.size()) + " items");
            return (int64_t)bytes.size();
        }
        case HostFn::UiCreate: {
            if (!HasPermission(addon, PermUiCreate, fn)) return ErrPermissionDenied;
            std::string kind;
            if (!readString(0, 1, kind)) return ErrInvalidMemory;
            if (kind != "frame" && kind != "text") return ErrInvalidArgument;
            if (addon.uiHandles.size() >= addon.limits.maxUiHandles) return ErrLimitExceeded;
            if (addon.nextUiSlot > 0xffff) return ErrLimitExceeded;
            uint64_t hostHandle = m_host ? m_host->UiCreate(addon.info.manifest.id, kind) : 0;
            if (!hostHandle) return ErrUnavailable;
            uint32_t slot = addon.nextUiSlot++;
            uint32_t generation = addon.uiGeneration & 0x7fff;
            addon.uiHandles[slot] = {0, generation, hostHandle, kind == "frame" ? UiKind::Frame : UiKind::FontString};
            RecordActivity(addon.info.manifest.id, "ui.create", kind);
            return (int32_t)((generation << 16) | slot);
        }
        case HostFn::UiSpawn: {
            if (!HasPermission(addon, PermUiCreate, fn)) return ErrPermissionDenied;
            if (args.size() < 3) return ErrInvalidArgument;
            std::string kind;
            if (!readString(1, 2, kind)) return ErrInvalidMemory;
            UiKind kindId = UiKind::None;
            if (kind == "frame") kindId = UiKind::Frame;
            else if (kind == "fontstring") kindId = UiKind::FontString;
            else if (kind == "texture") kindId = UiKind::Texture;
            else if (kind == "statusbar") kindId = UiKind::StatusBar;
            else if (kind == "button") kindId = UiKind::Button;
            else if (kind == "editbox") kindId = UiKind::EditBox;
            else if (kind == "scrollframe") kindId = UiKind::ScrollFrame;
            else return ErrInvalidArgument;
            uint64_t parentHost = 0;
            if (args[0] != 0) {
                UiHandle* parent = FindUi(addon, args[0]);
                if (!parent) return ErrInvalidHandle;
                if (!CanParent(parent->kind)) return ErrInvalidArgument;
                parentHost = parent->hostHandle;
            }
            if (addon.uiHandles.size() >= addon.limits.maxUiHandles) return ErrLimitExceeded;
            if (addon.nextUiSlot > 0xffff) return ErrLimitExceeded;
            uint64_t hostHandle = m_host ? m_host->UiSpawn(addon.info.manifest.id, parentHost, kind) : 0;
            if (!hostHandle) return ErrUnavailable;
            uint32_t slot = addon.nextUiSlot++;
            uint32_t generation = addon.uiGeneration & 0x7fff;
            addon.uiHandles[slot] = {0, generation, hostHandle, kindId};
            RecordActivity(addon.info.manifest.id, "ui.spawn", kind);
            return (int32_t)((generation << 16) | slot);
        }
        case HostFn::UiStyle: {
            if (!HasPermission(addon, PermUiCreate, fn)) return ErrPermissionDenied;
            if (args.size() < 4 || args[1] < 0 || args[2] < 0 || args[3] < 0) return ErrInvalidArgument;
            // ʕ •ᴥ•ʔ✿ Texture paths are longer than the other style blobs ✿ ʕ •ᴥ•ʔ
            if ((uint64_t)args[3] > ((int32_t)args[1] == (int32_t)UiOp::SetTexture ? 128u : 64u)) return ErrInvalidArgument;
            UiHandle* widget = FindUi(addon, args[0]);
            if (!widget) return ErrInvalidHandle;
            std::string raw;
            if (args[3] > 0 && !instance.ReadMemory((uint32_t)args[2], (uint32_t)args[3], raw)) return ErrInvalidMemory;
            std::vector<uint8_t> blob(raw.begin(), raw.end());
            int32_t op = (int32_t)args[1];
            auto need = [&](size_t n) { return blob.size() == n; };
            switch ((UiOp)op) {
                case UiOp::SetSize:
                    if (!need(8) || !InRange(ReadI32(blob.data()), 1, 2000) || !InRange(ReadI32(blob.data() + 4), 1, 2000)) return ErrInvalidArgument;
                    break;
                case UiOp::SetPoint: {
                    if (!need(16) || blob[0] > 8 || blob[1] > 8 || blob[2] || blob[3]) return ErrInvalidArgument;
                    int32_t rel = ReadI32(blob.data() + 4);
                    int32_t x = ReadI32(blob.data() + 8);
                    int32_t y = ReadI32(blob.data() + 12);
                    if (!InRange(x, -4000, 4000) || !InRange(y, -4000, 4000)) return ErrInvalidArgument;
                    uint64_t relHost = 0;
                    if (rel != 0) {
                        UiHandle* other = FindUi(addon, rel);
                        if (!other || other == widget) return other ? ErrInvalidArgument : ErrInvalidHandle;
                        relHost = other->hostHandle;
                    }
                    if (relHost > 0x7fffffff) return ErrLimitExceeded;
                    uint32_t packed = (uint32_t)relHost;
                    for (int i = 0; i < 4; i++) blob[4 + i] = (uint8_t)(packed >> (8 * i));
                    break;
                }
                case UiOp::SetColor:
                case UiOp::SetTextColor:
                case UiOp::SetBackdrop:
                    if (!need(4)) return ErrInvalidArgument;
                    break;
                case UiOp::SetShown:
                    if (!need(1) || blob[0] > 1) return ErrInvalidArgument;
                    break;
                case UiOp::SetValue: {
                    if (!need(12)) return ErrInvalidArgument;
                    int32_t value = ReadI32(blob.data());
                    int32_t minV = ReadI32(blob.data() + 4);
                    int32_t maxV = ReadI32(blob.data() + 8);
                    if (!InRange(value, -1000000000, 1000000000) || !InRange(minV, -1000000000, 1000000000) ||
                        !InRange(maxV, -1000000000, 1000000000) || minV > maxV) return ErrInvalidArgument;
                    break;
                }
                case UiOp::SetJustify:
                    if (!need(2) || blob[0] > 2 || blob[1] > 2) return ErrInvalidArgument;
                    break;
                case UiOp::SetFontSize:
                    if (!need(4) || !InRange(ReadI32(blob.data()), 6, 64)) return ErrInvalidArgument;
                    break;
                case UiOp::SetLevel:
                    if (!need(4) || !InRange(ReadI32(blob.data()), 0, 128)) return ErrInvalidArgument;
                    break;
                case UiOp::SetStrata:
                    if (!need(1) || blob[0] > 7) return ErrInvalidArgument;
                    break;
                case UiOp::SetAlpha:
                    if (!need(4) || !InRange(ReadI32(blob.data()), 0, 1000)) return ErrInvalidArgument;
                    break;
                case UiOp::SetMouse:
                    if (!need(1) || blob[0] > 1) return ErrInvalidArgument;
                    break;
                case UiOp::SetTexture:
                    if (!IsUiTexturePath(std::string(blob.begin(), blob.end()))) return ErrInvalidArgument;
                    break;
                default:
                    return ErrInvalidArgument;
            }
            return m_host && m_host->UiStyle(widget->hostHandle, op, blob.data(), (uint32_t)blob.size()) ? Ok : ErrUnavailable;
        }
        case HostFn::UiSetText: {
            if (!HasPermission(addon, PermUiCreate, fn)) return ErrPermissionDenied;
            if (args.size() < 3) return ErrInvalidArgument;
            UiHandle* widget = FindUi(addon, args[0]);
            if (!widget) return ErrInvalidHandle;
            if (args[2] < 0 || args[2] > 4096) return ErrLimitExceeded;
            std::string text;
            if (!instance.ReadMemory((uint32_t)args[1], (uint32_t)args[2], text)) return ErrInvalidMemory;
            text = SanitizeUiText(text);
            return m_host && m_host->UiSetText(widget->hostHandle, text) ? Ok : ErrUnavailable;
        }
        case HostFn::UiBind: {
            if (!HasPermission(addon, PermUiCreate, fn)) return ErrPermissionDenied;
            if (args.size() < 3 || args[1] < 0 || args[2] < 0 || args[2] > 4096) return ErrInvalidArgument;
            UiHandle* widget = FindUi(addon, args[0]);
            if (!widget) return ErrInvalidHandle;
            std::string event;
            if (!instance.ReadMemory((uint32_t)args[1], (uint32_t)args[2], event)) return ErrInvalidMemory;
            if (!IsUiBindEvent(event)) return ErrInvalidArgument;
            return m_host && m_host->UiBind(widget->hostHandle, (int32_t)args[0], event) ? Ok : ErrUnavailable;
        }
        case HostFn::UiGetText: {
            if (!HasPermission(addon, PermUiCreate, fn)) return ErrPermissionDenied;
            if (args.size() < 3 || args[1] < 0 || args[2] < 0) return ErrInvalidArgument;
            UiHandle* widget = FindUi(addon, args[0]);
            if (!widget) return ErrInvalidHandle;
            if (!m_host) return ErrUnavailable;
            std::string text;
            if (!m_host->UiGetText(widget->hostHandle, text)) return ErrUnavailable;
            if ((uint64_t)args[2] < text.size()) return ErrBufferTooSmall;
            if (!text.empty() && !instance.WriteMemory((uint32_t)args[1], text.data(), (uint32_t)text.size())) return ErrInvalidMemory;
            return (int64_t)text.size();
        }
        case HostFn::PerfQuery: {
            if (!HasPermission(addon, PermProfilingRead, fn)) return ErrPermissionDenied;
            switch (args.empty() ? -1 : args[0]) {
                case 0: return (int64_t)instance.MemoryBytes();
                case 1: return (int64_t)addon.info.stats.fuelUsed;
                case 2: return (int64_t)addon.info.stats.hostCalls;
                case 3: return (int64_t)addon.info.stats.eventsReceived;
                case 4: return (int64_t)addon.info.stats.traps;
                case 5: return (int64_t)addon.info.stats.denials;
                default: return ErrInvalidArgument;
            }
        }
        case HostFn::ChatSendRequest:
        case HostFn::AuctionBidRequest: {
            bool chat = fn == HostFn::ChatSendRequest;
            if (!HasPermission(addon, chat ? PermChatSend : PermAuctionWrite, fn)) return ErrPermissionDenied;
            if (addon.info.manifest.apiVersion < 2) return ErrUnavailable;
            ConfirmRequest request;
            request.addonIndex = IndexOf(addon);
            request.action = chat ? ConfirmAction::ChatSend : ConfirmAction::AuctionBid;
            if (chat) {
                if (args.size() < 5 || !IsAllowedChatType((int32_t)args[0])) return ErrInvalidArgument;
                request.chatType = (int32_t)args[0];
                std::string message;
                if (!readString(1, 2, request.target) || !readString(3, 4, message)) return ErrInvalidMemory;
                auto cleanText = [](const std::string& s, size_t max) {
                    if (s.empty() || s.size() > max) return false;
                    // ʕ ●ᴥ●ʔ✿ Valid UTF-8 only; bidi overrides and zero-width marks can fake the dialog text ✿ ʕ ●ᴥ●ʔ
                    for (size_t i = 0; i < s.size();) {
                        uint8_t c = (uint8_t)s[i];
                        if (c < 0x20 || c == 0x7f || c == '|') return false;
                        if (c < 0x80) { i++; continue; }
                        uint32_t cp = 0;
                        int extra = 0;
                        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
                        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
                        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
                        else return false;
                        if (i + (size_t)extra >= s.size()) return false;
                        for (int k = 1; k <= extra; k++) {
                            if (((uint8_t)s[i + k] & 0xC0) != 0x80) return false;
                            cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
                        }
                        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
                        if ((cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) ||
                            (cp >= 0x200B && cp <= 0x200D) || cp == 0xFEFF) return false;
                        i += (size_t)extra + 1;
                    }
                    return true;
                };
                if (!cleanText(message, 255) || (!request.target.empty() && !cleanText(request.target, 64))) return ErrInvalidArgument;
                bool whisper = request.chatType == 7;
                bool channel = request.chatType == 17;
                if (whisper && request.target.empty()) return ErrInvalidArgument;
                if (channel && (request.target.empty() || request.target.find_first_not_of("0123456789") != std::string::npos)) return ErrInvalidArgument;
                if (!whisper && !channel && !request.target.empty()) return ErrInvalidArgument;
                request.message = message;
            } else {
                if (args.size() < 3 || args[0] < 0 || args[0] > 2 || args[1] < 1 || args[2] <= 0 || args[2] > 2000000000) return ErrInvalidArgument;
                request.listKind = (int32_t)args[0];
                request.auctionIndex = (uint32_t)args[1];
                request.bid = (uint32_t)args[2];
                if (!m_host || !m_host->ResolveAuctionIdentity(request.listKind, request.auctionIndex, request.identity)) return ErrUnavailable;
            }
            switch (m_confirm.Add(request, Now())) {
                case ConfirmQueue::Submit::Queued: {
                    const ConfirmRequest* front = m_confirm.Front();
                    if (front && front->id == request.id && m_host) m_host->ConfirmShow(*front);
                    RecordActivity(addon.info.manifest.id, chat ? "chat.send.request" : "auction.bid.request", std::to_string(request.id));
                    return (int64_t)request.id;
                }
                case ConfirmQueue::Submit::AutoDenied:
                    addon.info.stats.denials++;
                    return ErrPermissionDenied;
                default:
                    return ErrLimitExceeded;
            }
        }
        case HostFn::UnitSnapshot: {
            if (!HasPermission(addon, PermUnitsRead, fn)) return ErrPermissionDenied;
            std::string blob;
            if (!readString(0, 1, blob)) return ErrInvalidMemory;
            std::vector<std::string> tokens;
            size_t pos = 0;
            while (pos <= blob.size()) {
                size_t nul = blob.find('\0', pos);
                std::string token = blob.substr(pos, nul == std::string::npos ? nul : nul - pos);
                if (!token.empty()) tokens.push_back(token);
                if (nul == std::string::npos) break;
                pos = nul + 1;
            }
            if (tokens.empty() || tokens.size() > 64) return ErrInvalidArgument;
            for (auto& token : tokens) if (!IsValidUnitToken(token)) return ErrInvalidArgument;
            if (!m_host || args.size() < 4 || args[2] < 0 || args[3] < 0) return ErrInvalidArgument;
            std::vector<UnitRecord> records;
            if (!m_host->GetUnitSnapshot(tokens, records)) return ErrUnavailable;
            std::vector<uint8_t> bytes;
            PutU32(bytes, (uint32_t)records.size());
            for (auto& r : records) {
                PutU32(bytes, r.tokenIndex); PutU32(bytes, r.flags); PutU32(bytes, r.level); PutU32(bytes, r.classId);
                PutU32(bytes, r.raceId); PutU32(bytes, r.health); PutU32(bytes, r.healthMax); PutU32(bytes, r.powerType);
                PutU32(bytes, r.power); PutU32(bytes, r.powerMax);
            }
            if ((uint64_t)args[3] < bytes.size()) return ErrBufferTooSmall;
            if (!instance.WriteMemory((uint32_t)args[2], bytes.data(), (uint32_t)bytes.size())) return ErrInvalidMemory;
            RecordActivity(addon.info.manifest.id, "units.read", std::to_string(records.size()) + " units");
            return (int64_t)bytes.size();
        }
        case HostFn::UnitNames: {
            if (!HasPermission(addon, PermUnitsRead, fn)) return ErrPermissionDenied;
            std::string blob;
            if (!readString(0, 1, blob)) return ErrInvalidMemory;
            std::vector<std::string> tokens;
            size_t pos = 0;
            while (pos <= blob.size()) {
                size_t nul = blob.find('\0', pos);
                std::string token = blob.substr(pos, nul == std::string::npos ? nul : nul - pos);
                if (!token.empty()) tokens.push_back(token);
                if (nul == std::string::npos) break;
                pos = nul + 1;
            }
            if (tokens.empty() || tokens.size() > 64) return ErrInvalidArgument;
            for (auto& token : tokens) if (!IsValidUnitToken(token)) return ErrInvalidArgument;
            if (!m_host || args.size() < 4 || args[2] < 0 || args[3] < 0) return ErrInvalidArgument;
            std::vector<std::pair<uint32_t, std::string>> records;
            if (!m_host->GetUnitNames(tokens, records)) return ErrUnavailable;
            std::vector<uint8_t> bytes;
            PutU32(bytes, (uint32_t)records.size());
            for (auto& rec : records) {
                std::string name = SanitizeDisplayText(rec.second);
                if (name.size() > 64) name.resize(64);
                PutU32(bytes, rec.first);
                PutU16(bytes, (uint16_t)name.size());
                bytes.insert(bytes.end(), name.begin(), name.end());
            }
            if ((uint64_t)args[3] < bytes.size()) return ErrBufferTooSmall;
            if (!instance.WriteMemory((uint32_t)args[2], bytes.data(), (uint32_t)bytes.size())) return ErrInvalidMemory;
            RecordActivity(addon.info.manifest.id, "units.names", std::to_string(records.size()) + " names");
            return (int64_t)bytes.size();
        }
        case HostFn::AuctionSnapshot: {
            if (!HasPermission(addon, PermAuctionRead, fn)) return ErrPermissionDenied;
            if (args.size() < 3 || args[0] < 0 || args[0] > 2 || args[1] < 0 || args[2] < 0) return ErrInvalidArgument;
            if (!m_host) return ErrUnavailable;
            AuctionSnapshot snapshot;
            if (!m_host->GetAuctionSnapshot((int32_t)args[0], snapshot)) return ErrUnavailable;
            std::vector<uint8_t> bytes;
            PutU32(bytes, (uint32_t)snapshot.records.size());
            PutU32(bytes, snapshot.total);
            PutU32(bytes, snapshot.flags);
            for (auto& r : snapshot.records) {
                PutU32(bytes, r.index); PutU32(bytes, r.itemId); PutU32(bytes, r.count); PutU32(bytes, r.minBid);
                PutU32(bytes, r.minIncrement); PutU32(bytes, r.buyout); PutU32(bytes, r.bid); PutU32(bytes, r.timeLeft);
                PutU32(bytes, r.quality); PutU32(bytes, r.flags);
            }
            if ((uint64_t)args[2] < bytes.size()) return ErrBufferTooSmall;
            if (!instance.WriteMemory((uint32_t)args[1], bytes.data(), (uint32_t)bytes.size())) return ErrInvalidMemory;
            RecordActivity(addon.info.manifest.id, "auction.read", std::to_string(snapshot.records.size()) + " auctions");
            return (int64_t)bytes.size();
        }
        case HostFn::StorageGet:
        case HostFn::StorageLen:
        case HostFn::StorageKeys:
        case HostFn::StorageUsage:
        case HostFn::StorageSet:
        case HostFn::StorageDelete: {
            bool write = fn == HostFn::StorageSet || fn == HostFn::StorageDelete;
            if (!HasPermission(addon, write ? PermStorageWrite : PermStorageRead, fn)) return ErrPermissionDenied;
            if (args.empty() || (args[0] != 0 && args[0] != 1)) return ErrInvalidArgument;
            int scope = (int)args[0];
            if (StorageDir(scope).empty()) return ErrUnavailable;
            auto& storeMap = addon.storage[scope];
            auto readKey = [&](size_t a, size_t b, std::string& key) {
                if (!readString(a, b, key) || key.empty() || key.size() > kMaxStorageKeyBytes) return false;
                for (char c : key) if ((unsigned char)c < 0x20) return false;
                return true;
            };
            if (fn == HostFn::StorageUsage) {
                uint64_t total = 0;
                for (auto& [k, v] : storeMap) total += k.size() + v.size();
                return (int64_t)total;
            }
            if (fn == HostFn::StorageKeys) {
                if (args.size() < 3 || args[1] < 0 || args[2] < 0) return ErrInvalidArgument;
                std::vector<uint8_t> bytes;
                PutU32(bytes, (uint32_t)storeMap.size());
                for (auto& [k, v] : storeMap) { PutU16(bytes, (uint16_t)k.size()); bytes.insert(bytes.end(), k.begin(), k.end()); }
                if ((uint64_t)args[2] < bytes.size()) return ErrBufferTooSmall;
                return instance.WriteMemory((uint32_t)args[1], bytes.data(), (uint32_t)bytes.size()) ? (int64_t)bytes.size() : ErrInvalidMemory;
            }
            std::string key;
            if (args.size() < 3 || args[1] < 0 || args[2] < 0) return ErrInvalidArgument;
            if (!instance.ReadMemory((uint32_t)args[1], (uint32_t)std::min<int64_t>(args[2], kMaxStorageKeyBytes + 1), key)) return ErrInvalidMemory;
            if (key.empty() || key.size() > kMaxStorageKeyBytes) return ErrInvalidArgument;
            for (char c : key) if ((unsigned char)c < 0x20) return ErrInvalidArgument;
            auto it = storeMap.find(key);
            if (fn == HostFn::StorageLen) return it == storeMap.end() ? ErrNotFound : (int64_t)it->second.size();
            if (fn == HostFn::StorageDelete) {
                if (it == storeMap.end()) return ErrNotFound;
                storeMap.erase(it);
                addon.storageDirty[scope] = true;
                return Ok;
            }
            if (fn == HostFn::StorageGet) {
                if (it == storeMap.end()) return ErrNotFound;
                if (args.size() < 5 || args[3] < 0 || args[4] < 0) return ErrInvalidArgument;
                if ((uint64_t)args[4] < it->second.size()) return ErrBufferTooSmall;
                addon.storageBytesInCallback += it->second.size();
                if (addon.storageBytesInCallback > kMaxStorageBytesPerCallback) return ErrLimitExceeded;
                return instance.WriteMemory((uint32_t)args[3], it->second.data(), (uint32_t)it->second.size()) ? (int64_t)it->second.size() : ErrInvalidMemory;
            }
            // ʕ •ᴥ•ʔ✿ StorageSet: quota and key caps are enforced here, never trusted from the manifest ✿ ʕ •ᴥ•ʔ
            if (args.size() < 5 || args[3] < 0 || args[4] < 0 || args[4] > (int64_t)kMaxStorageValueBytes) return ErrLimitExceeded;
            std::string value;
            if (!instance.ReadMemory((uint32_t)args[3], (uint32_t)args[4], value)) return ErrInvalidMemory;
            uint64_t total = 0;
            for (auto& [k, v] : storeMap) total += k.size() + v.size();
            if (it != storeMap.end()) total -= it->first.size() + it->second.size();
            if (it == storeMap.end() && storeMap.size() >= kMaxStorageKeysPerScope) return ErrLimitExceeded;
            if (total + key.size() + value.size() > addon.limits.storageQuotaBytes) return ErrLimitExceeded;
            addon.storageBytesInCallback += value.size();
            if (addon.storageBytesInCallback > kMaxStorageBytesPerCallback) return ErrLimitExceeded;
            storeMap[key] = std::vector<uint8_t>(value.begin(), value.end());
            addon.storageDirty[scope] = true;
            RecordActivity(addon.info.manifest.id, "storage.write", std::string(scope ? "character " : "account ") + std::to_string(value.size()) + " bytes");
            return Ok;
        }
        default: return ErrGeneric;
    }
}

size_t WasmAddonManager::ScanAndLoadAll() {
    size_t loaded = 0;
    std::error_code ec;
    if (!fs::is_directory(m_rootDir, ec)) { RecordActivity("", "scan", "root missing: " + m_rootDir); return 0; }
    std::vector<fs::path> dirs;
    for (auto& entry : fs::directory_iterator(m_rootDir, ec))
        if (entry.is_directory() && fs::exists(entry.path() / "addon.toml")) dirs.push_back(entry.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto& dir : dirs) if (LoadAddon(dir.string())) loaded++;
    return loaded;
}

bool WasmAddonManager::LoadAddon(const std::string& directory) {
    auto addon = std::make_unique<Addon>();
    addon->info.directory = directory;
    std::vector<uint8_t> manifestBytes;
    if (!ReadFileLimited(fs::path(directory) / "addon.toml", kMaxManifestBytes, manifestBytes)) {
        RecordActivity(directory, "load.failed", "manifest missing/oversized");
        return false;
    }
    std::string error;
    Manifest manifest;
    if (!ParseManifest(std::string(manifestBytes.begin(), manifestBytes.end()), manifest, error)) {
        RecordActivity(directory, "load.failed", error);
        return false;
    }
    addon->info.manifest = manifest;
    addon->info.state = AddonState::Discovered;
    int replaceIndex = -1;
    for (size_t i = 0; i < m_addons.size(); i++) {
        if (m_addons[i]->info.manifest.id != manifest.id) continue;
        AddonState state = m_addons[i]->info.state;
        if (state == AddonState::Loaded || state == AddonState::Running || state == AddonState::Suspended) {
            RecordActivity(manifest.id, "load.failed", "duplicate addon id");
            return false;
        }
        replaceIndex = (int)i;
    }
    auto store = [&](std::unique_ptr<Addon> value) {
        if (replaceIndex >= 0) m_addons[replaceIndex] = std::move(value);
        else m_addons.push_back(std::move(value));
    };
    std::vector<uint8_t> moduleBytes;
    if (!ReadFileLimited(fs::path(directory) / manifest.moduleFile, kMaxModuleBytes, moduleBytes)) {
        addon->info.state = AddonState::Failed;
        RecordError(*addon, "module missing/oversized");
        store(std::move(addon));
        return false;
    }
    addon->info.sha256 = Sha256Hex(moduleBytes.data(), moduleBytes.size());
    addon->info.signaturePresent = fs::exists(fs::path(directory) / "addon.sig");
    addon->limits = manifest.requestedLimits;
    if (!m_runtime) {
        addon->info.state = AddonState::Failed;
        RecordError(*addon, "WASM runtime unavailable");
        store(std::move(addon));
        return false;
    }
    RuntimeInfo runtimeInfo;
    if (!m_runtime->Validate(moduleBytes, addon->info.sha256, runtimeInfo, error)) {
        addon->info.state = AddonState::Failed;
        addon->info.prohibitedImports = runtimeInfo.prohibitedImports;
        RecordError(*addon, "validation failed: " + error);
        store(std::move(addon));
        return false;
    }
    addon->info.importedFunctions = runtimeInfo.imports;
    if (replaceIndex >= 0) {
        SaveStorage(*m_addons[replaceIndex], 0);
        SaveStorage(*m_addons[replaceIndex], 1);
        if (m_host) {
            for (auto& [slot, handle] : m_addons[replaceIndex]->uiHandles) m_host->UiDestroy(handle.hostHandle);
            m_addons[replaceIndex]->uiHandles.clear();
        }
    }
    addon->info.state = AddonState::Validated;
    MigrateDenyEntries(manifest, addon->info.sha256);
    addon->info.grantedPermissions = EffectiveGrants(manifest, addon->info.sha256);
    addon->info.deniedPermissions = manifest.declaredPermissions & ~addon->info.grantedPermissions;
    addon->uiGeneration = ++m_nextUiGeneration;
    Addon* raw = addon.get();
    auto dispatch = [this, raw](IWasmInstance& instance, HostFn fn, const std::vector<int64_t>& args) {
        return HandleHostCall(*raw, instance, fn, args);
    };
    addon->instance = m_runtime->Instantiate(moduleBytes, addon->info.sha256, addon->limits, dispatch, error);
    if (!addon->instance) {
        addon->info.state = AddonState::Failed;
        RecordError(*addon, "instantiate failed: " + error);
        store(std::move(addon));
        return false;
    }
    LoadStorage(*addon, 0);
    LoadStorage(*addon, 1);
    if (manifest.apiVersion >= 2 && !addon->instance->HasExport("wow_on_event_v2")) {
        addon->info.state = AddonState::Failed;
        RecordError(*addon, "api_version 2 requires the wow_on_event_v2 export");
        store(std::move(addon));
        return false;
    }
    addon->info.state = AddonState::Loaded;
    store(std::move(addon));
    RecordActivity(manifest.id, "load", manifest.version + " sha256=" + raw->info.sha256);
    m_confirm.ResetAddon(IndexOf(*raw));
    raw->info.state = AddonState::Running;
    int64_t result = 0;
    if (!InvokeExport(*raw, "wow_on_load", {}, result)) return false;
    if (result != Ok) { FailAddon(*raw, "wow_on_load returned " + std::to_string(result)); return false; }
    return true;
}

bool WasmAddonManager::UnloadAddon(const std::string& addonId) {
    for (auto& addonPtr : m_addons) {
        Addon& addon = *addonPtr;
        if (addon.info.manifest.id != addonId || addon.info.state == AddonState::Unloaded) continue;
        // ʕ ●ᴥ●ʔ✿ A faulted addon runs no more guest code, and its unflushed writes are discarded ✿ ʕ ●ᴥ●ʔ
        CancelConfirmFor(IndexOf(addon));
        bool wasRunning = addon.info.state == AddonState::Running;
        addon.subscriptionsSet.clear();
        if (addon.instance && wasRunning) {
            if (addon.instance->HasExport("wow_on_unload")) {
                int64_t ignored = 0;
                InvokeExport(addon, "wow_on_unload", {}, ignored);
            }
            for (auto& [slot, handle] : addon.uiHandles) if (m_host) m_host->UiDestroy(handle.hostHandle);
        }
        if (wasRunning) { SaveStorage(addon, 0); SaveStorage(addon, 1); }
        addon.uiHandles.clear();
        addon.instance.reset();
        addon.info.state = AddonState::Unloaded;
        addon.info.activeUiHandles = 0;
        RecordActivity(addonId, "unload", "");
        return true;
    }
    return false;
}

void WasmAddonManager::InvalidateUiHandles() {
    for (auto& addon : m_addons) {
        addon->uiHandles.clear();
        addon->info.activeUiHandles = 0;
        addon->nextUiSlot = 1;
        addon->uiGeneration = ++m_nextUiGeneration;
    }
    RecordActivity("", "ui.invalidate", "script state reset");
}

bool WasmAddonManager::HasSubscriber(const std::string& eventName) const {
    for (auto& addon : m_addons)
        if (addon->info.state == AddonState::Running && addon->subscriptionsSet.count(eventName)) return true;
    return false;
}

// ʕ ●ᴥ●ʔ✿ Rebuilds a WEV1 payload with every argument replaced by a redacted tag, preserving argc ✿ ʕ ●ᴥ●ʔ
static std::vector<uint8_t> RedactPayload(const std::vector<uint8_t>& payload) {
    if (payload.size() < 18 || std::memcmp(payload.data(), "WEV1", 4) != 0) return payload;
    uint16_t nameLen = (uint16_t)(payload[16] | (payload[17] << 8));
    if (18 + (size_t)nameLen > payload.size()) return payload;
    std::string name((const char*)payload.data() + 18, nameLen);
    EventPayloadWriter writer(name, 0);
    size_t pos = 18 + nameLen;
    while (pos < payload.size()) {
        uint8_t tag = payload[pos++];
        writer.Redacted();
        switch (tag) {
            case 0: case 1: case 5: break;
            case 2: pos += 8; break;
            case 3: if (pos + 4 <= payload.size()) pos += 4 + GetU32(payload.data() + pos); break;
            case 4: if (pos + 8 <= payload.size()) pos += 8 + GetU32(payload.data() + pos + 4); break;
            default: return writer.Finish();
        }
        if (pos > payload.size()) break;
    }
    return writer.Finish();
}

void WasmAddonManager::DispatchEvent(const std::string& eventName, const std::vector<uint8_t>& payload) {
    if (eventName == "AUCTION_HOUSE_CLOSED" || eventName == "AUCTION_ITEM_LIST_UPDATE") {
        const ConfirmRequest* before = m_confirm.Front();
        m_confirm.CancelAuctions(Now());
        const ConfirmRequest* after = m_confirm.Front();
        if (before && (!after || after->id != before->id) && m_host) {
            m_host->ConfirmHide(before->id);
            ShowNextConfirm();
        }
    }
    if (m_dispatching || m_callbackDepth > 0) { m_queuedEvents.push_back({eventName, payload}); return; }
    m_dispatching = true;
    m_dispatchBudget = 1024;
    QueuedEvent current{eventName, payload};
    while (true) {
        std::vector<uint8_t> effective = current.payload;
        if (effective.empty()) {
            EventPayloadWriter writer(current.name, 0);
            effective = writer.Finish();
        }
        bool combat = current.name == "COMBAT_LOG_EVENT" || current.name == "COMBAT_LOG_EVENT_UNFILTERED";
        for (auto& addonPtr : m_addons) {
            Addon& addon = *addonPtr;
            if (addon.info.state != AddonState::Running || !addon.subscriptionsSet.count(current.name) || !addon.instance) continue;
            if (m_dispatchBudget == 0) { RecordActivity(addon.info.manifest.id, "event.budget", "delivery budget exhausted; dropping event chain"); continue; }
            m_dispatchBudget--;
            addon.info.stats.eventsReceived++;
            // ʕ •ᴥ•ʔ✿ Chat and combat payloads are withheld without the matching sensitive permission ✿ ʕ •ᴥ•ʔ
            bool chat = current.name.size() >= 9 && current.name.compare(0, 9, "CHAT_MSG_") == 0;
            bool unit = current.name.size() >= 5 && current.name.compare(0, 5, "UNIT_") == 0;
            bool gated = (chat && !(addon.info.grantedPermissions & PermChatRead)) ||
                         (combat && !(addon.info.grantedPermissions & PermCombatRead)) ||
                         (unit && !(addon.info.grantedPermissions & PermUnitsRead));
            std::vector<uint8_t> bytes = gated ? RedactPayload(effective) : effective;
            bool v2 = addon.info.manifest.apiVersion >= 2;
            if (!v2 && !addon.instance->HasExport("wow_on_event")) continue;
            if (v2 && !addon.instance->HasExport("wow_on_event_v2")) continue;
            const std::vector<uint8_t>& data = v2 ? bytes : effective;
            const char* exportName = v2 ? "wow_on_event_v2" : "wow_on_event";
            // ʕ ◕ᴥ◕ ʔ✿ v1 addons receive just the event name, exactly as before ✿ ʕ ◕ᴥ◕ ʔ
            const uint8_t* ptr = v2 ? data.data() : (const uint8_t*)current.name.data();
            uint32_t len = v2 ? (uint32_t)data.size() : (uint32_t)current.name.size();
            int64_t allocPtr = 0, ignored = 0;
            if (!addon.instance->HasExport("wow_alloc") || !InvokeExport(addon, "wow_alloc", {(int64_t)len}, allocPtr) || allocPtr <= 0) {
                if (addon.info.state == AddonState::Running) RecordError(addon, "event delivery requires wow_alloc export");
                continue;
            }
            if (!addon.instance->WriteMemory((uint32_t)allocPtr, ptr, len)) { RecordError(addon, "event buffer out of bounds"); continue; }
            InvokeExport(addon, exportName, {allocPtr, (int64_t)len}, ignored);
            if (addon.info.state == AddonState::Running && addon.instance->HasExport("wow_free")) InvokeExport(addon, "wow_free", {allocPtr, (int64_t)len}, ignored);
        }
        if (m_queuedEvents.empty()) break;
        current = std::move(m_queuedEvents.front());
        m_queuedEvents.erase(m_queuedEvents.begin());
    }
    m_dispatching = false;
}

bool WasmAddonManager::CallExportInt(const std::string& addonId, const char* exportName, int64_t& out) {
    for (auto& addon : m_addons)
        if (addon->info.manifest.id == addonId && addon->instance && addon->info.state == AddonState::Running)
            return InvokeExport(*addon, exportName, {}, out);
    return false;
}
bool WasmAddonManager::CallExportIntArg(const std::string& addonId, const char* exportName, int64_t arg, int64_t& out) {
    for (auto& addon : m_addons)
        if (addon->info.manifest.id == addonId && addon->instance && addon->info.state == AddonState::Running)
            return InvokeExport(*addon, exportName, {arg}, out);
    return false;
}

std::vector<InspectorInfo> WasmAddonManager::GetInspector() const {
    std::vector<InspectorInfo> out;
    for (auto& addon : m_addons) {
        InspectorInfo info = addon->info;
        info.subscriptions.assign(addon->subscriptionsSet.begin(), addon->subscriptionsSet.end());
        info.activeUiHandles = (uint32_t)addon->uiHandles.size();
        for (int scope = 0; scope < 2; scope++) {
            info.storageKeys[scope] = (uint32_t)addon->storage[scope].size();
            info.storageBytes[scope] = 0;
            for (auto& [k, v] : addon->storage[scope]) info.storageBytes[scope] += k.size() + v.size();
        }
        if (addon->instance) info.stats.memoryBytes = addon->instance->MemoryBytes();
        out.push_back(std::move(info));
    }
    return out;
}

std::string WasmAddonManager::FormatInspectorText() const {
    std::ostringstream out;
    out << "WASM Addons (" << m_addons.size() << ") runtime=" << (RuntimeAvailable() ? "wasmtime" : "unavailable") << "\n";
    for (auto& info : GetInspector()) {
        out << info.manifest.id << " \"" << SanitizeDisplayText(info.manifest.name) << "\" v" << SanitizeDisplayText(info.manifest.version)
            << " state=" << ToString(info.state) << " sha256=" << info.sha256.substr(0, 16) << "..."
            << " signature=" << (info.signaturePresent ? "present(unverified)" : "none")
            << " memory=" << info.stats.memoryBytes << " fuel=" << info.stats.fuelUsed
            << " hostCalls=" << info.stats.hostCalls << " storage acct=" << info.storageBytes[0] << "B/" << info.storageKeys[0] << " char=" << info.storageBytes[1] << "B/" << info.storageKeys[1] << " events=" << info.stats.eventsReceived
            << " traps=" << info.stats.traps << " denials=" << info.stats.denials
            << " ui=" << info.activeUiHandles << "\n  granted:";
        for (Permission p : AllPermissions()) if (info.grantedPermissions & p) out << ' ' << PermissionName(p);
        out << "\n  denied:";
        for (Permission p : AllPermissions()) if (info.deniedPermissions & p) out << ' ' << PermissionName(p);
        out << "\n  imports:";
        for (auto& f : info.importedFunctions) out << ' ' << SanitizeDisplayText(f);
        for (auto& f : info.prohibitedImports) out << " PROHIBITED:" << SanitizeDisplayText(f);
        if (!info.lastError.empty()) out << "\n  lastError: " << SanitizeDisplayText(info.lastError);
        out << "\n";
    }
    out << "Note: few imports or a signature do not make an addon safe; grants above are enforced, activity below is observed.\n";
    for (auto& e : m_activity) out << '[' << e.timestampMs << "] " << e.addonId << ' ' << e.kind << ' ' << SanitizeDisplayText(e.detail) << "\n";
    return out.str();
}

} // namespace WasmAddon
