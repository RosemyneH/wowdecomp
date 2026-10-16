#pragma once
#include "Engine/Source/WasmAddon/WasmManifest.h"
#include "Engine/Source/WasmAddon/WasmConfirm.h"
#include "Engine/Source/WasmAddon/WasmRuntime.h"
#include <functional>
#include <map>
#include <memory>
#include <set>

namespace WasmAddon {

// ʕ •ᴥ•ʔ✿ Main-thread only: all public methods must be called from the client main thread ✿ ʕ •ᴥ•ʔ
class WasmAddonManager {
public:
    bool Initialize(const std::string& rootDir, IWasmHostServices* host, const std::string& permissionFile = "");
    void Shutdown();
    bool IsInitialized() const { return m_initialized; }
    const std::string& RootDir() const { return m_rootDir; }

    size_t ScanAndLoadAll();
    bool LoadAddon(const std::string& directory);
    bool UnloadAddon(const std::string& addonId);
    void DispatchEvent(const std::string& eventName, const std::vector<uint8_t>& payload = {});
    bool HasSubscriber(const std::string& eventName) const;
    bool CallExportInt(const std::string& addonId, const char* exportName, int64_t& out);
    void TickConfirm();
    bool ResolveConfirm(uint32_t requestId, bool accept);
    void RearmConfirm();
    void SetClockForTests(std::function<uint64_t()> nowFn) { m_nowFn = std::move(nowFn); }
    size_t ConfirmQueueSize() const { return m_confirm.Size(); }
    void InvalidateUiHandles();
    bool CallExportIntArg(const std::string& addonId, const char* exportName, int64_t arg, int64_t& out);

    void SetPermissionOverride(const std::string& addonId, Permission perm, bool granted);
    void SetStorageDirs(const std::string& accountDir, const std::string& characterDir);
    bool ClearStorage(const std::string& addonId, int scope);
    bool GrantSensitive(const std::string& addonId, Permission perm);
    std::vector<InspectorInfo> GetInspector() const;
    std::string FormatInspectorText() const;
    std::vector<ActivityEntry> GetActivity() const { return {m_activity.begin(), m_activity.end()}; }

private:
    struct UiHandle { uint32_t addonIndex = 0; uint32_t generation = 0; uint64_t hostHandle = 0; };
    struct Addon {
        InspectorInfo info;
        Limits limits;
        std::unique_ptr<IWasmInstance> instance;
        std::set<std::string> subscriptionsSet;
        std::map<uint32_t, UiHandle> uiHandles;
        uint32_t nextUiSlot = 1;
        uint32_t uiGeneration = 0;
        std::map<std::string, std::vector<uint8_t>> storage[2];
        bool storageDirty[2] = {};
        uint64_t storageBytesInCallback = 0;
        uint32_t hostCallsInCallback = 0;
    };

    int64_t HandleHostCall(Addon& addon, IWasmInstance& instance, HostFn fn, const std::vector<int64_t>& args);
    bool HasPermission(Addon& addon, Permission perm, HostFn fn);
    void RecordActivity(const std::string& addonId, const std::string& kind, const std::string& detail);
    void RecordError(Addon& addon, const std::string& error);
    void FailAddon(Addon& addon, const std::string& error);
    uint32_t EffectiveGrants(const Manifest& manifest, const std::string& sha256) const;
    void LoadPermissionStore();
    void MigrateDenyEntries(const Manifest& manifest, const std::string& sha256);
    void SavePermissionStore() const;
    bool InvokeExport(Addon& addon, const char* name, const std::vector<int64_t>& args, int64_t& result);
    uint64_t Now() const;
    size_t IndexOf(const Addon& addon) const;
    void CancelConfirmFor(size_t addonIndex);
    void ShowNextConfirm();
    void DeliverConfirmResult(const ConfirmQueue::Completed& completed, int32_t detail);
    void DispatchEventTo(size_t addonIndex, const std::string& name, const std::vector<uint8_t>& payload);
    void LoadStorage(Addon& addon, int scope);
    void SaveStorage(Addon& addon, int scope);
    std::string StorageDir(int scope) const { return scope == 0 ? m_storageAccountDir : m_storageCharacterDir; }
    static bool IsKnownEvent(const std::string& name);
    static std::vector<uint8_t> SerializeInventory(const std::vector<InventoryItem>& items);

    std::string m_rootDir;
    std::string m_permissionFile;
    bool m_permissionStoreLocked = false;
    std::string m_storageAccountDir;
    std::string m_storageCharacterDir;
    IWasmHostServices* m_host = nullptr;
    std::unique_ptr<IWasmRuntime> m_runtime;
    std::vector<std::unique_ptr<Addon>> m_addons;
    std::vector<ActivityEntry> m_activity;
    std::map<std::string, bool> m_permissionStore;
    bool m_initialized = false;
    struct QueuedEvent { std::string name; std::vector<uint8_t> payload; };
    bool m_dispatching = false;
    uint32_t m_callbackDepth = 0;
    uint32_t m_nextUiGeneration = 0;
    uint32_t m_dispatchBudget = 0;
    void FlushQueuedEvents();
    std::vector<QueuedEvent> m_queuedEvents;
    ConfirmQueue m_confirm;
    std::function<uint64_t()> m_nowFn;
};

} // namespace WasmAddon
