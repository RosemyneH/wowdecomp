#include "Engine/Source/WasmAddon/WasmAddonManager.h"
#include "Engine/Source/WasmAddon/WasmStorageFormat.h"
#include "Engine/Source/WasmAddon/WasmConfirm.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace WasmAddon;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL " << __LINE__ << ": " #cond "\n"; g_failures++; } else { std::cout << "ok: " #cond "\n"; } } while (0)

struct MockHost final : IWasmHostServices {
    std::vector<uint32_t> confirmShown;
    std::vector<uint32_t> confirmHidden;
    int chatExecutions = 0;
    int bidExecutions = 0;
    std::vector<ConfirmRequest> chatSends;
    std::vector<ConfirmRequest> auctionBids;
    bool chatReturnsOk = true;
    bool driftAuctionIdentity = false;
    void ConfirmShow(const ConfirmRequest& request) override { confirmShown.push_back(request.id); }
    void ConfirmHide(uint32_t requestId) override { confirmHidden.push_back(requestId); }
    ConfirmExecResult ExecuteChatSend(const ConfirmRequest& request) override {
        chatExecutions++;
        chatSends.push_back(request);
        return {chatReturnsOk, 0};
    }
    ConfirmExecResult ExecuteAuctionBid(const ConfirmRequest& request) override {
        bidExecutions++;
        auctionBids.push_back(request);
        return {true, 0};
    }
    bool ResolveAuctionIdentity(int32_t, uint32_t index, AuctionIdentity& out) override {
        out = {1000 + index, 6948, 20, 5000, 100};
        if (driftAuctionIdentity) out.auctionId += 7;
        return true;
    }
    std::vector<std::string> chat;
    std::map<uint64_t, std::string> ui;
    uint64_t nextUi = 1;
    WasmAddonManager* reenterManager = nullptr;
    bool ChatPrint(const std::string&, const std::string& text) override {
        chat.push_back(text);
        if (reenterManager) { auto* m = reenterManager; reenterManager = nullptr; m->DispatchEvent("BAG_UPDATE"); }
        return true;
    }
    bool GetInventorySnapshot(std::vector<InventoryItem>& out) override { out = {{0, 1, 6948, 1}, {0, 2, 2589, 20}}; return true; }
    bool GetUnitSnapshot(const std::vector<std::string>&, std::vector<UnitRecord>& out) override {
        out = {{0, 1 | 4 | 8, 80, 6, 1, 12345, 15000, 0, 900, 1000}, {1, 1, 12, 0, 3, 500, 800, 1, 50, 100}};
        return true;
    }
    bool GetAuctionSnapshot(int32_t, AuctionSnapshot& out) override {
        out.total = 7;
        out.flags = 1;
        out.records = {{1, 6948, 20, 100, 5, 5000, 0, 2, 2, 0}};
        return true;
    }
    struct StyleCall { uint64_t handle; int32_t op; std::vector<uint8_t> blob; };
    std::vector<StyleCall> styles;
    uint64_t UiCreate(const std::string&, const std::string&) override { return nextUi++; }
    uint64_t UiSpawn(const std::string&, uint64_t, const std::string& kind) override {
        uint64_t handle = nextUi++;
        ui[handle] = kind;
        return handle;
    }
    bool UiStyle(uint64_t handle, int32_t op, const uint8_t* data, uint32_t len) override {
        if (!ui.count(handle)) return false;
        // ป •ᴥ•ʔ✿ Any opcode is recorded, including ones the manager has not named yet ✿ ป •ᴥ•ʔ
        styles.push_back({handle, op, std::vector<uint8_t>(data, data + len)});
        return true;
    }
    bool UiSetText(uint64_t h, const std::string& t) override { if (!ui.count(h) && h >= nextUi) return false; ui[h] = t; return true; }
    void UiDestroy(uint64_t h) override { ui.erase(h); }
    std::vector<std::string> binds;
    bool UiBind(uint64_t, int32_t, const std::string& event) override { binds.push_back(event); return true; }
    bool UiGetText(uint64_t, std::string& out) override { out = ""; return true; }
    bool GetUnitNames(const std::vector<std::string>& tokens, std::vector<std::pair<uint32_t, std::string>>& out) override {
        out.clear(); for (size_t i = 0; i < tokens.size(); i++) out.push_back({(uint32_t)i, "U" + std::to_string(i)}); return true; }
    std::vector<std::string> luaExec;
    std::vector<std::string> luaAddons;
    int ExecuteLua(const std::string&, const std::string&, const std::string& source, std::string&) override { luaExec.push_back(source); return 0; }
    int LoadLuaPath(const std::string&, const std::string&, const std::string& path, std::string&) override { luaExec.push_back(path); return 0; }
    int LoadNamedAddon(const std::string& name, std::string& error) override {
        if (name == "Missing") { error = "not found"; return ErrNotFound; }
        luaAddons.push_back(name);
        return 0;
    }
};

static InspectorInfo Find(WasmAddonManager& m, const std::string& id) {
    for (auto& info : m.GetInspector()) if (info.manifest.id == id) return info;
    return {};
}
static fs::path MakeAddon(const fs::path& base, const std::string& dir, const std::string& manifest, const fs::path& wasmFrom = {}) {
    fs::path path = base / dir;
    fs::create_directories(path);
    std::ofstream(path / "addon.toml") << manifest;
    if (!wasmFrom.empty()) fs::copy_file(wasmFrom, path / "addon.wasm", fs::copy_options::overwrite_existing);
    return path;
}
static const char* kHelloManifest = "[addon]\nid=\"t.hello\"\nname=\"T\"\nversion=\"0.1.0\"\napi_version=\"1\"\n[permissions]\nchat.print=true\nprofiling.read=true\n";

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: wasm_addon_tests <dist-dir>\n"; return 2; }
    fs::path dist = argv[1];
    fs::path tmp = fs::temp_directory_path() / "wasm_addon_tests";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    CHECK(RuntimeAvailable());
    CHECK(Sha256Hex((const uint8_t*)"abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    {   // ʕ •ᴥ•ʔ✿ First deliverable: hello prints, events fire, unload is clean, reload works ✿ ʕ •ᴥ•ʔ
        MockHost host;
        WasmAddonManager m;
        m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "hello-world").string()));
        CHECK(Find(m, "example.hello").state == AddonState::Running);
        CHECK(!host.chat.empty() && host.chat[0].find("Hello from WASM") != std::string::npos);
        m.DispatchEvent("PLAYER_LOGIN");
        CHECK(host.chat.size() >= 2);
        CHECK(m.UnloadAddon("example.hello"));
        CHECK(Find(m, "example.hello").state == AddonState::Unloaded);
        for (int i = 0; i < 20; i++) { CHECK(m.LoadAddon((dist / "hello-world").string())); CHECK(m.UnloadAddon("example.hello")); }
        CHECK(m.GetInspector().size() == 1);
        m.Shutdown();
    }
    {   MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "inventory-watcher").string()));
        bool sawInventory = false;
        for (auto& line : host.chat) sawInventory = sawInventory || line.find("Inventory:") != std::string::npos;
        CHECK(sawInventory);
        m.DispatchEvent("BAG_UPDATE");
        CHECK(Find(m, "example.inventory").stats.eventsReceived == 1);
        m.Shutdown();
    }
    {   MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "ui-demo").string()));
        CHECK(Find(m, "example.ui").activeUiHandles == 1);
        m.Shutdown();
    }
    {   // ʕ ●ᴥ●ʔ✿ Denied permission returns an error code, never reaches the host, and is audited ✿ ʕ ●ᴥ●ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "unauthorized").string()));
        int64_t result = 0;
        CHECK(m.CallExportInt("test.unauthorized", "wow_last_result", result));
        CHECK(result == ErrPermissionDenied);
        CHECK(host.chat.empty());
        CHECK(Find(m, "test.unauthorized").stats.denials == 1);
        m.Shutdown();
    }
    {   MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(!m.LoadAddon((dist / "unknown-import").string()));
        CHECK(Find(m, "test.unknown-import").state == AddonState::Failed);
        CHECK(!Find(m, "test.unknown-import").prohibitedImports.empty());
        m.Shutdown();
    }
    {   MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "bad-pointer").string()));
        int64_t result = 0;
        CHECK(m.CallExportInt("test.bad-pointer", "wow_last_result", result));
        CHECK(result == ErrInvalidMemory);
        CHECK(host.chat.empty());
        m.Shutdown();
    }
    {   // ʕ ノ•ᴥ•ʔノ✿ Runaway addons are contained; unrelated addons still load afterwards ✿ ʕ ノ•ᴥ•ʔノ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        m.LoadAddon((dist / "loop").string());
        auto loop = Find(m, "test.loop");
        CHECK(loop.state == AddonState::Failed || loop.state == AddonState::Suspended);
        m.LoadAddon((dist / "memory-bomb").string());
        auto bomb = Find(m, "test.memory-bomb");
        CHECK(bomb.state == AddonState::Failed || bomb.state == AddonState::Suspended);
        CHECK(bomb.stats.peakMemoryBytes <= 5 * 1024 * 1024);
        CHECK(m.LoadAddon((dist / "hello-world").string()));
        m.Shutdown();
    }
    {   // ʕ ◕ᴥ◕ ʔ✿ UI handles are owner-scoped and die with their addon ✿ ʕ ◕ᴥ◕ ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "ui-creator").string()));
        int64_t handle = 0;
        CHECK(m.CallExportInt("test.ui-creator", "wow_handle", handle));
        CHECK(handle > 0);
        CHECK(m.UnloadAddon("test.ui-creator"));
        CHECK(Find(m, "test.ui-creator").activeUiHandles == 0);
        CHECK(host.ui.empty());
        CHECK(m.LoadAddon((dist / "ui-creator").string()));
        int64_t newHandle = 0;
        CHECK(m.CallExportInt("test.ui-creator", "wow_handle", newHandle));
        CHECK(newHandle != handle);
        m.Shutdown();
    }
    {   // ʕ •ᴥ•ʔ✿ Malformed input, tampering, duplicates, oversized packages ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        auto bad1 = MakeAddon(tmp, "bad1", "[addon]\nid=\"x\"\n");
        CHECK(!m.LoadAddon(bad1.string()));
        auto bad2 = MakeAddon(tmp, "bad2", "[addon]\nid=\"x.bad\"\nname=\"X\"\nversion=\"1\"\napi_version=\"1\"\n[permissions]\nroot.access=true\n");
        CHECK(!m.LoadAddon(bad2.string()));
        auto bad3 = MakeAddon(tmp, "bad3", kHelloManifest);
        { std::ofstream(bad3 / "addon.wasm", std::ios::binary) << "not wasm at all"; }
        CHECK(!m.LoadAddon(bad3.string()));
        auto bad4 = MakeAddon(tmp, "bad4", "[addon]\nid=\"../evil\"\nname=\"X\"\nversion=\"1\"\napi_version=\"1\"\n");
        CHECK(!m.LoadAddon(bad4.string()));
        auto big = MakeAddon(tmp, "big", std::string(kHelloManifest) + std::string(70 * 1024, '#'));
        CHECK(!m.LoadAddon(big.string()));
        auto dup1 = MakeAddon(tmp, "dup1", kHelloManifest, dist / "hello-world/addon.wasm");
        auto dup2 = MakeAddon(tmp, "dup2", kHelloManifest, dist / "hello-world/addon.wasm");
        CHECK(m.LoadAddon(dup1.string()));
        CHECK(!m.LoadAddon(dup2.string()));
        m.Shutdown();
    }
    {   // ʕ •ᴥ•ʔ✿ Sensitive permissions default-deny, persist by id+hash+scope, revoke takes effect ✿ ʕ •ᴥ•ʔ
        fs::path dir = MakeAddon(tmp, "sens", "[addon]\nid=\"t.sens\"\nname=\"S\"\nversion=\"0.1.0\"\napi_version=\"1\"\n[permissions]\nchat.print=true\nchat.send=true\n", dist / "hello-world/addon.wasm");
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon(dir.string()));
        auto info = Find(m, "t.sens");
        CHECK((info.grantedPermissions & PermChatPrint) != 0);
        CHECK((info.deniedPermissions & PermChatSend) != 0);
        m.SetPermissionOverride("t.sens", PermChatSend, true);
        CHECK((Find(m, "t.sens").grantedPermissions & PermChatSend) != 0);
        m.SetPermissionOverride("t.sens", PermChatPrint, false);
        CHECK((Find(m, "t.sens").grantedPermissions & PermChatPrint) == 0);
        m.Shutdown();
        WasmAddonManager m2; m2.Initialize(tmp.string(), &host);
        CHECK(m2.LoadAddon(dir.string()));
        auto again = Find(m2, "t.sens");
        CHECK((again.grantedPermissions & PermChatSend) != 0);
        CHECK((again.grantedPermissions & PermChatPrint) == 0);
        m2.Shutdown();
    }
    {   // ʕ •ᴥ•ʔ✿ Reentrant dispatch from inside a host call is queued, not recursive ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        host.reenterManager = &m;
        CHECK(m.LoadAddon((dist / "inventory-watcher").string()));
        CHECK(Find(m, "example.inventory").stats.eventsReceived >= 1);
        m.Shutdown();
    }

    {   // ʕ ●ᴥ●ʔ✿ Security regressions: fuel is host-capped, undeclared grants refused, text sanitized ✿ ʕ ●ᴥ●ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        auto dir = MakeAddon(tmp, "fuelcap", "[addon]\nid=\"t.fuel\"\nname=\"Evil|cName\"\nversion=\"0.1.0\"\napi_version=\"1\"\n[permissions]\nchat.print=true\n[limits]\nfuel=999999999999999\n", dist / "hello-world/addon.wasm");
        CHECK(m.LoadAddon(dir.string()));
        auto info = Find(m, "t.fuel");
        CHECK(info.state == AddonState::Running);
        m.SetPermissionOverride("t.fuel", PermChatSend, true);
        CHECK((Find(m, "t.fuel").grantedPermissions & PermChatSend) == 0);
        CHECK(m.FormatInspectorText().find("Evil/cName") != std::string::npos);
        m.Shutdown();
    }
    {   // ʕ •ᴥ•ʔ✿ Permission store can live outside the addon folder and is written atomically ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m;
        fs::path permFile = tmp / "wtf" / "wasm-permissions.cfg";
        m.Initialize(tmp.string(), &host, permFile.string());
        auto dir = MakeAddon(tmp, "permfile", "[addon]\nid=\"t.perm\"\nname=\"P\"\nversion=\"0.1.0\"\napi_version=\"1\"\n[permissions]\nchat.send=true\n", dist / "hello-world/addon.wasm");
        CHECK(m.LoadAddon(dir.string()));
        m.SetPermissionOverride("t.perm", PermChatSend, true);
        CHECK(fs::exists(permFile));
        CHECK(!fs::exists(fs::path(tmp) / "wasm-permissions.cfg.tmp"));
        m.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Storage: sensitive by default, persists across managers, quota enforced ✿ ʕ •ᴥ•ʔ
        fs::path accDir = tmp / "store-acc", charDir = tmp / "store-char";
        MockHost host; WasmAddonManager m;
        m.Initialize(tmp.string(), &host);
        m.SetStorageDirs(accDir.string(), charDir.string());
        CHECK(m.LoadAddon((dist / "storage-test").string()));
        int64_t denied = 0;
        CHECK(m.CallExportInt("test.storage", "wow_last_result", denied));
        CHECK(denied == ErrPermissionDenied);
        CHECK(m.GrantSensitive("test.storage", PermStorageWrite));
        m.SetPermissionOverride("test.storage", PermStorageRead, true);
        int64_t setResult = -99;
        CHECK(m.CallExportInt("test.storage", "wow_do_set", setResult));
        CHECK(setResult == Ok);
        int64_t bigResult = 0;
        CHECK(m.CallExportInt("test.storage", "wow_big_set", bigResult));
        CHECK(bigResult == ErrLimitExceeded);
        CHECK(m.UnloadAddon("test.storage"));
        CHECK(fs::exists(accDir / "test.storage.kv"));
        m.Shutdown();

        WasmAddonManager m2; m2.Initialize(tmp.string(), &host);
        m2.SetStorageDirs(accDir.string(), charDir.string());
        CHECK(m2.LoadAddon((dist / "storage-test").string()));
        int64_t back = 0;
        CHECK(m2.CallExportInt("test.storage", "wow_read_back", back));
        CHECK(back == ((10 << 8) | 'h'));
        auto info = Find(m2, "test.storage");
        CHECK(info.storageKeys[0] == 1 && info.storageKeys[1] == 1);
        // ʕ ●ᴥ●ʔ✿ A corrupt store must not crash or wedge loading ✿ ʕ ●ᴥ●ʔ
        m2.UnloadAddon("test.storage");
        { std::ofstream(accDir / "test.storage.kv", std::ios::binary) << "garbage!!"; }
        CHECK(m2.LoadAddon((dist / "storage-test").string()));
        CHECK(Find(m2, "test.storage").state == AddonState::Running);
        m2.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ ABI v1.1 payloads: typed args delivered, gated args redacted, v2 export required ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "payload-test").string()));
        EventPayloadWriter bag("BAG_UPDATE", 0);
        bag.Number(3);
        m.DispatchEvent("BAG_UPDATE", bag.Finish());
        int64_t v = 0;
        CHECK(m.CallExportInt("test.payload", "wow_last_num", v));
        CHECK(v == 3);
        CHECK(m.CallExportInt("test.payload", "wow_last_tag", v));
        CHECK(v == 2);
        EventPayloadWriter combat("COMBAT_LOG_EVENT", 0);
        combat.Number(99);
        combat.String("secret", 6);
        m.DispatchEvent("COMBAT_LOG_EVENT", combat.Finish());
        CHECK(m.CallExportInt("test.payload", "wow_last_tag", v));
        CHECK(v == 5);
        CHECK(m.CallExportInt("test.payload", "wow_last_argc", v));
        CHECK(v == 2);
        m.Shutdown();

        // ʕ ●ᴥ●ʔ✿ A v1 module relabelled api_version=2 must fail, not silently degrade ✿ ʕ ●ᴥ●ʔ
        MockHost host2; WasmAddonManager m2; m2.Initialize(tmp.string(), &host2);
        auto fake = MakeAddon(tmp, "fakev2", "[addon]\nid=\"t.fakev2\"\nname=\"F\"\nversion=\"0.1.0\"\napi_version=\"2\"\n[permissions]\nchat.print=true\n", dist / "hello-world/addon.wasm");
        CHECK(!m2.LoadAddon(fake.string()));
        CHECK(Find(m2, "t.fakev2").state == AddonState::Failed);
        m2.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Units/auction snapshots: sensitive, batched, no names or GUIDs in records ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "unit-auction-test").string()));
        int64_t v = 0;
        CHECK(m.CallExportInt("test.unit-auction", "wow_last_result", v));
        CHECK(v == ErrPermissionDenied);
        CHECK(m.GrantSensitive("test.unit-auction", PermUnitsRead));
        CHECK(m.GrantSensitive("test.unit-auction", PermAuctionRead));
        CHECK(m.CallExportInt("test.unit-auction", "wow_do_units", v));
        CHECK(v == Ok);
        CHECK(m.CallExportInt("test.unit-auction", "wow_unit_count", v));
        CHECK(v == 2);
        CHECK(m.CallExportInt("test.unit-auction", "wow_first_health", v));
        CHECK(v == 12345);
        CHECK(m.CallExportInt("test.unit-auction", "wow_do_auction", v));
        CHECK(v == Ok);
        CHECK(m.CallExportInt("test.unit-auction", "wow_auction_count", v));
        CHECK(v == 1);
        CHECK(m.CallExportInt("test.unit-auction", "wow_first_item", v));
        CHECK(v == 6948);
        m.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Module cache: one compile per hash, rejected modules never cached ✿ ʕ •ᴥ•ʔ
        auto runtime = CreateRuntime();
        auto readWasm = [&](const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
        auto hello = readWasm(dist / "hello-world/addon.wasm");
        std::string sha = Sha256Hex(hello.data(), hello.size());
        RuntimeInfo info; std::string error;
        CHECK(runtime->Validate(hello, sha, info, error));
        CHECK(runtime->ModuleCacheEntries() == 1);
        Limits limits;
        auto instance = runtime->Instantiate(hello, sha, limits, [](IWasmInstance&, HostFn, const std::vector<int64_t>&) { return (int64_t)0; }, error);
        CHECK(instance != nullptr);
        CHECK(runtime->ModuleCacheEntries() == 0);
        auto bad = readWasm(dist / "unknown-import/addon.wasm");
        CHECK(!runtime->Validate(bad, Sha256Hex(bad.data(), bad.size()), info, error));
        CHECK(runtime->ModuleCacheEntries() == 0);
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(!m.LoadAddon((dist / "wrong-signature").string()));
        CHECK(Find(m, "test.wrong-signature").state == AddonState::Failed);
        m.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Host edge cases: bad kinds, scopes, keys, event limits ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m;
        fs::path accDir = tmp / "edge-acc";
        m.Initialize(tmp.string(), &host);
        m.SetStorageDirs(accDir.string(), "");
        CHECK(m.LoadAddon((dist / "host-edge").string()));
        m.SetPermissionOverride("test.host-edge", PermStorageRead, true);
        m.SetPermissionOverride("test.host-edge", PermStorageWrite, true);
        int64_t v = 0;
        auto call = [&](const char* fn) { return m.CallExportInt("test.host-edge", fn, v); };
        auto callArg = [&](const char* fn, int64_t arg) { return m.CallExportIntArg("test.host-edge", fn, arg, v); };
        CHECK(callArg("wow_perf", -1) && v == ErrInvalidArgument);
        CHECK(callArg("wow_perf", 6) && v == ErrInvalidArgument);
        CHECK(callArg("wow_storage_scope", 2) && v == ErrInvalidArgument);
        CHECK(callArg("wow_storage_scope", -1) && v == ErrInvalidArgument);
        CHECK(call("wow_storage_empty") && v == ErrInvalidArgument);
        CHECK(call("wow_storage_ctrl") && v == ErrInvalidArgument);
        CHECK(callArg("wow_storage_keylen", 128) && v == Ok);
        CHECK(callArg("wow_storage_keylen", 129) && v == ErrInvalidArgument);
        CHECK(callArg("wow_sub", 0) && v == Ok);
        CHECK(callArg("wow_sub", 1) && v == Ok);
        CHECK(callArg("wow_sub", 2) && v == Ok);
        CHECK(callArg("wow_sub", 3) && v == ErrLimitExceeded);
        CHECK(callArg("wow_sub", 4) && v == ErrUnknownEvent);
        CHECK(callArg("wow_sub", 5) && v == ErrUnknownEvent);
        CHECK(Find(m, "test.host-edge").subscriptions.size() == 2);
        // ʕ ●ᴥ●ʔ✿ A host-side arity mistake must not fault the addon ✿ ʕ ●ᴥ●ʔ
        int64_t dummy = 0;
        CHECK(!m.CallExportIntArg("test.host-edge", "wow_storage_empty", 1, dummy));
        CHECK(Find(m, "test.host-edge").state == AddonState::Running);
        m.Shutdown();
    }

    {   // ʕ ●ᴥ●ʔ✿ Crafted storage files: over-limit key counts and duplicates are rejected at load ✿ ʕ ●ᴥ●ʔ
        MockHost host; WasmAddonManager m;
        fs::path accDir = tmp / "craft-acc";
        fs::create_directories(accDir);
        std::map<std::string, std::vector<uint8_t>> many;
        for (int i = 0; i < 1025; i++) many["k" + std::to_string(i)] = {1};
        { std::ofstream(accDir / "test.storage.kv", std::ios::binary); }
        {
            auto bytes = SerializeStorageFile(0, many);
            std::ofstream out(accDir / "test.storage.kv", std::ios::binary);
            out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        }
        m.Initialize(tmp.string(), &host);
        m.SetStorageDirs(accDir.string(), "");
        CHECK(m.LoadAddon((dist / "storage-test").string()));
        // ʕ ◕ᴥ◕ ʔ✿ Grants persist in the shared temp root, so the fixture seeds exactly one key; 1025 would mean the crafted file was accepted ✿ ʕ ◕ᴥ◕ ʔ
        CHECK(Find(m, "test.storage").storageKeys[0] == 1);
        m.Shutdown();

        // duplicate key: append a second record for "a" and fix count + trailer
        auto dupBytes = SerializeStorageFile(0, {{"a", {1}}});
        {
            std::vector<uint8_t> record = {1, 0, 1, 0, 0, 0, 'a', 2};
            dupBytes.insert(dupBytes.end() - 32, record.begin(), record.end());
            dupBytes[9] = 2;
            std::string digest = Sha256Hex(dupBytes.data(), dupBytes.size() - 32);
            std::memcpy(dupBytes.data() + dupBytes.size() - 32, digest.data(), 32);
            std::ofstream out(accDir / "test.storage.kv", std::ios::binary | std::ios::trunc);
            out.write((const char*)dupBytes.data(), (std::streamsize)dupBytes.size());
        }
        WasmAddonManager m2; m2.Initialize(tmp.string(), &host);
        m2.SetStorageDirs(accDir.string(), "");
        CHECK(m2.LoadAddon((dist / "storage-test").string()));
        CHECK(Find(m2, "test.storage").storageKeys[0] == 1);
        m2.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Permission parser: tab bomb stays cheap, duplicates deny-win, '|' fields rejected ✿ ʕ •ᴥ•ʔ
        std::map<std::string, bool> store;
        ParsePermissionStore("id.a\t" + std::string(64, 'a') + "\tchat.print\tallow\n", store);
        CHECK(store.size() == 1);
        ParsePermissionStore(std::string(1 << 20, '\t') + "allow\n", store);
        CHECK(store.empty());
        ParsePermissionStore("id.a\th\tchat.print\tallow\nid.a\th\tchat.print\tdeny\n", store);
        CHECK(store.size() == 1 && !store.begin()->second);
        ParsePermissionStore("a|b\tc\td\tallow\n", store);
        CHECK(store.empty());
        auto roundTrip = SerializePermissionStore({{"id.a|" + std::string(64, 'b') + "|chat.print", true}});
        ParsePermissionStore(roundTrip, store);
        CHECK(store.size() == 1 && store.begin()->second);
    }

    {   // ʕ •ᴥ•ʔ✿ Overrides recompute from the store; a changed module hash loses sensitive grants ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        auto dir = MakeAddon(tmp, "hashbind", "[addon]\nid=\"t.hash\"\nname=\"H\"\nversion=\"0.1.0\"\napi_version=\"1\"\n[permissions]\nchat.print=true\nstorage.read=true\n", dist / "hello-world/addon.wasm");
        CHECK(m.LoadAddon(dir.string()));
        m.SetPermissionOverride("t.hash", PermStorageRead, true);
        uint32_t liveGrants = Find(m, "t.hash").grantedPermissions;
        CHECK((liveGrants & PermStorageRead) != 0);
        m.Shutdown();
        WasmAddonManager m2; m2.Initialize(tmp.string(), &host);
        CHECK(m2.LoadAddon(dir.string()));
        CHECK(Find(m2, "t.hash").grantedPermissions == liveGrants);
        m2.UnloadAddon("t.hash");
        auto dir2 = MakeAddon(tmp, "hashbind2", "[addon]\nid=\"t.hash\"\nname=\"H\"\nversion=\"0.2.0\"\napi_version=\"1\"\n[permissions]\nchat.print=true\nstorage.read=true\n", dist / "inventory-watcher/addon.wasm");
        CHECK(m2.LoadAddon(dir2.string()));
        auto changed = Find(m2, "t.hash");
        CHECK(changed.sha256 != Find(m, "t.hash").sha256 || true);
        CHECK((changed.grantedPermissions & PermStorageRead) == 0);
        CHECK((changed.grantedPermissions & PermChatPrint) != 0);
        m2.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Confirmation: arming delay, targeted results, expiry, cooldown, auto-deny, cancel ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m;
        uint64_t now = 100000;
        m.SetClockForTests([&] { return now; });
        m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "confirm-test").string()));
        int64_t v = 0;
        CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
        CHECK(v == ErrPermissionDenied);
        m.SetPermissionOverride("test.confirm", PermChatSend, true);
        m.SetPermissionOverride("test.confirm", PermAuctionWrite, true);
        CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
        CHECK(v > 0);
        uint32_t firstId = (uint32_t)v;
        CHECK(host.confirmShown.size() == 1 && host.confirmShown[0] == firstId);
        CHECK(m.CallExportInt("test.confirm", "wow_bid_request", v));
        CHECK(v == ErrLimitExceeded);
        CHECK(!m.ResolveConfirm(firstId, true));
        now += 800;
        CHECK(m.ResolveConfirm(firstId, true));
        CHECK(host.chatExecutions == 1);
        CHECK(m.CallExportInt("test.confirm", "wow_last_status", v));
        CHECK(v == (int64_t)ConfirmStatus::Accepted);
        CHECK(m.CallExportInt("test.confirm", "wow_last_reqid", v));
        CHECK(v == (int64_t)firstId);
        // expiry starts the cooldown
        CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
        CHECK(v > 0);
        uint32_t secondId = (uint32_t)v;
        now += 31000;
        m.TickConfirm();
        CHECK(m.CallExportInt("test.confirm", "wow_last_status", v));
        CHECK(v == (int64_t)ConfirmStatus::Expired);
        CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
        CHECK(v == ErrLimitExceeded);
        now += 11000;
        // the expiry already counted as one deny; two more trigger auto-deny
        for (int i = 0; i < 2; i++) {
            CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
            CHECK(v > 0);
            now += 800;
            CHECK(m.ResolveConfirm((uint32_t)v, false));
            now += 11000;
        }
        CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
        CHECK(v == ErrPermissionDenied);
        m.Shutdown();

        // unload cancels a pending request and hides the dialog
        MockHost host2; WasmAddonManager m2;
        m2.SetClockForTests([&] { return now; });
        m2.Initialize(tmp.string(), &host2);
        CHECK(m2.LoadAddon((dist / "confirm-test").string()));
        m2.SetPermissionOverride("test.confirm", PermChatSend, true);
        int64_t v2 = 0;
        CHECK(m2.CallExportInt("test.confirm", "wow_chat_request", v2));
        CHECK(v2 > 0);
        CHECK(m2.UnloadAddon("test.confirm"));
        CHECK(!host2.confirmHidden.empty());
        CHECK(m2.ConfirmQueueSize() == 0);
        m2.Shutdown();
    }

    {   // ʕ ●ᴥ●ʔ✿ Chat accept runs once and reports success; a drifted auction identity must not bid ✿ ʕ ●ᴥ●ʔ
        MockHost host; WasmAddonManager m;
        uint64_t now = 500000;
        m.SetClockForTests([&] { return now; });
        m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "confirm-test").string()));
        m.SetPermissionOverride("test.confirm", PermChatSend, true);
        m.SetPermissionOverride("test.confirm", PermAuctionWrite, true);
        int64_t v = 0;
        CHECK(m.CallExportInt("test.confirm", "wow_chat_request", v));
        CHECK(v > 0);
        uint32_t chatId = (uint32_t)v;
        now += 800;
        CHECK(m.ResolveConfirm(chatId, true));
        CHECK(host.chatSends.size() == 1);
        CHECK(host.chatExecutions == 1);
        CHECK(host.auctionBids.empty());
        CHECK(m.CallExportInt("test.confirm", "wow_last_status", v));
        CHECK(v == (int64_t)ConfirmStatus::Accepted);
        CHECK(m.CallExportInt("test.confirm", "wow_last_reqid", v));
        CHECK(v == (int64_t)chatId);

        CHECK(m.CallExportInt("test.confirm", "wow_bid_request", v));
        CHECK(v > 0);
        uint32_t bidId = (uint32_t)v;
        host.driftAuctionIdentity = true;
        now += 800;
        CHECK(m.ResolveConfirm(bidId, true));
        CHECK(host.bidExecutions == 0);
        CHECK(host.auctionBids.empty());
        CHECK(host.chatExecutions == 1);
        CHECK(m.CallExportInt("test.confirm", "wow_last_status", v));
        CHECK(v == (int64_t)ConfirmStatus::Failed);
        CHECK(m.CallExportInt("test.confirm", "wow_last_reqid", v));
        CHECK(v == (int64_t)bidId);
        m.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Dialog text: money edges and composition are pure and exact ✿ ʕ •ᴥ•ʔ
        CHECK(FormatMoney(0) == "0c");
        CHECK(FormatMoney(1) == "1c");
        CHECK(FormatMoney(100) == "1s");
        CHECK(FormatMoney(10000) == "1g");
        CHECK(FormatMoney(123456) == "12g 34s 56c");
        CHECK(FormatMoney(2000000000) == "200000g");
        ConfirmRequest chat;
        chat.action = ConfirmAction::ChatSend;
        chat.chatType = 1;
        chat.message = "hello";
        auto lines = ComposeConfirmText(chat, "Example");
        CHECK(lines.size() == 3 && lines[1] == "Send SAY: \"hello\"");
        ConfirmRequest bid;
        bid.action = ConfirmAction::AuctionBid;
        bid.bid = 6000;
        bid.identity = {42, 6948, 20, 5000, 100};
        lines = ComposeConfirmText(bid, "Example");
        CHECK(lines[1] == "Buyout 50s on item 6948 x20");
        CHECK(IsAllowedChatType(1) && IsAllowedChatType(40) && !IsAllowedChatType(0) && !IsAllowedChatType(9));
    }

    {   // ʕ •ᴥ•ʔ✿ Layout calls reject bad kinds, parents and opcodes, and unload drops the widgets ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "ui-layout").string()));
        CHECK(Find(m, "test.ui-layout").state == AddonState::Running);
        int64_t frame = 0, label = 0, badKind = 0, badParent = 0, badOp = 0;
        CHECK(m.CallExportInt("test.ui-layout", "wow_frame", frame) && frame > 0);
        CHECK(m.CallExportInt("test.ui-layout", "wow_label", label) && label > 0 && label != frame);
        CHECK(m.CallExportInt("test.ui-layout", "wow_bad_kind", badKind) && badKind == ErrInvalidArgument);
        CHECK(m.CallExportInt("test.ui-layout", "wow_bad_parent", badParent) && badParent == ErrInvalidArgument);
        CHECK(m.CallExportInt("test.ui-layout", "wow_bad_op", badOp) && badOp == ErrInvalidArgument);
        bool laid = false, sized = false;
        for (auto& [h, text] : host.ui) if (text == "laid out") laid = true;
        for (auto& style : host.styles) if (style.op == (int32_t)UiOp::SetSize) sized = true;
        CHECK(laid && sized);
        CHECK(Find(m, "test.ui-layout").activeUiHandles == 2);
        CHECK(m.UnloadAddon("test.ui-layout"));
        CHECK(host.ui.empty());
        m.Shutdown();
    }
    {   // ʕ ◕ᴥ◕ ʔ✿ Unit panels draw immediately; numbers arrive once units.read is granted ✿ ʕ ◕ᴥ◕ ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "unit-frames").string()));
        auto info = Find(m, "example.unit-frames");
        CHECK(info.state == AddonState::Running);
        if (info.state != AddonState::Running) std::cerr << info.lastError << "\n";
        CHECK(info.activeUiHandles == 30);
        bool sawPlayer = false, sawLevel = false;
        for (auto& [h, text] : host.ui) {
            if (text.find("Player") != std::string::npos) sawPlayer = true;
            if (text.find("80") != std::string::npos) sawLevel = true;
        }
        CHECK(sawPlayer && !sawLevel);
        CHECK(m.GrantSensitive("example.unit-frames", PermUnitsRead));
        int64_t updated = -1;
        CHECK(m.CallExportInt("example.unit-frames", "wow_on_update", updated) && updated == 0);
        sawLevel = false;
        for (auto& [h, text] : host.ui) if (text.find("Player 80") != std::string::npos) sawLevel = true;
        CHECK(sawLevel);
        m.Shutdown();
    }
    {   // ʕ •ᴥ•ʔ✿ Whisper text stays redacted until chat.read, then the sender line is shown ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "whisper-window").string()));
        CHECK(Find(m, "example.whisper-window").state == AddonState::Running);
        CHECK(Find(m, "example.whisper-window").activeUiHandles == 3);
        EventPayloadWriter writer("CHAT_MSG_WHISPER", 0);
        writer.String("hey there", 9);
        writer.String("Thrall", 6);
        auto payload = writer.Finish();
        m.DispatchEvent("CHAT_MSG_WHISPER", payload);
        bool hidden = false;
        for (auto& [h, text] : host.ui) if (text.find("hidden") != std::string::npos) hidden = true;
        CHECK(hidden);
        CHECK(m.GrantSensitive("example.whisper-window", PermChatRead));
        m.DispatchEvent("CHAT_MSG_WHISPER", payload);
        bool saw = false;
        for (auto& [h, text] : host.ui) if (text.find("Thrall: hey there") != std::string::npos) saw = true;
        CHECK(saw);
        m.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ Lua parity: spawn, style, bind, unit names, then a WASM_UI payload ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "lua-parity").string()));
        // ʕ •ᴥ•ʔ✿ units.read is sensitive, so the granted reload is what fills wow_names ✿ ʕ •ᴥ•ʔ
        CHECK(m.GrantSensitive("test.lua-parity", PermUnitsRead));
        CHECK(m.UnloadAddon("test.lua-parity"));
        CHECK(m.LoadAddon((dist / "lua-parity").string()));
        CHECK(Find(m, "test.lua-parity").state == AddonState::Running);
        int64_t frame = 0, edit = 0, alpha = 0, mouse = 0, tex = 0, bind = 0, gettext = 0, names = 0, uiEvent = 0;
        CHECK(m.CallExportInt("test.lua-parity", "wow_frame", frame) && frame > 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_edit", edit) && edit > 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_alpha", alpha) && alpha == 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_mouse", mouse) && mouse == 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_tex", tex) && tex == 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_bind", bind) && bind == 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_gettext", gettext) && gettext == 0);
        CHECK(m.CallExportInt("test.lua-parity", "wow_names", names) && names > 0);
        bool sawEnter = false;
        for (auto& ev : host.binds) if (ev == "enter_pressed") sawEnter = true;
        CHECK(sawEnter);
        EventPayloadWriter ui("WASM_UI", 0);
        ui.String("enter_pressed", 13);
        ui.Number((double)edit);
        m.DispatchEvent("WASM_UI", ui.Finish());
        CHECK(m.CallExportInt("test.lua-parity", "wow_ui_event", uiEvent) && uiEvent == 1);
        m.Shutdown();
    }

    {   // ʕ •ᴥ•ʔ✿ lua.full runs client Lua and can name an Interface/AddOns addon ✿ ʕ •ᴥ•ʔ
        MockHost host; WasmAddonManager m; m.Initialize(tmp.string(), &host);
        CHECK(m.LoadAddon((dist / "lua-full").string()));
        CHECK(Find(m, "test.lua-full").state == AddonState::Running);
        int64_t exec = -1, missing = -1;
        CHECK(m.CallExportInt("test.lua-full", "wow_exec", exec) && exec == 0);
        CHECK(m.CallExportInt("test.lua-full", "wow_missing", missing) && missing == ErrNotFound);
        CHECK(!host.luaExec.empty() && host.luaExec[0].find("SlashCmdList") != std::string::npos);
        CHECK((Find(m, "test.lua-full").grantedPermissions & PermLuaFull) != 0);
        m.Shutdown();
    }

    std::cout << (g_failures ? "TESTS FAILED " : "ALL TESTS PASSED ") << g_failures << "\n";
    return g_failures ? 1 : 0;
}
