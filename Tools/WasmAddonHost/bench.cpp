#include "Engine/Source/WasmAddon/WasmAddonManager.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace fs = std::filesystem;
using namespace WasmAddon;

struct BenchHost final : IWasmHostServices {
    bool ChatPrint(const std::string&, const std::string&) override { return true; }
    bool GetInventorySnapshot(std::vector<InventoryItem>& out) override {
        out.clear();
        for (uint32_t bag = 0; bag < 5; bag++)
            for (uint32_t slot = 1; slot <= 20; slot++) out.push_back({bag, slot, 1000 + slot, 5});
        return true;
    }
};


// ʕ •ᴥ•ʔ✿ Phase 10 sections: startup, scale/frame-time, combat payload cost ✿ ʕ •ᴥ•ʔ
struct SectionHost final : IWasmHostServices {
    bool ChatPrint(const std::string&, const std::string&) override { return true; }
    bool GetInventorySnapshot(std::vector<InventoryItem>& out) override { out = {{0, 1, 6948, 1}}; return true; }
};

static void PrintStats(const char* name, std::vector<double> values) {
    if (values.empty()) { std::cout << "bench." << name << "=skipped\n"; return; }
    std::sort(values.begin(), values.end());
    auto q = [&](double f) { return values[std::min(values.size() - 1, (size_t)(f * (double)values.size()))]; };
    double mean = 0;
    for (double v : values) mean += v;
    std::cout << "bench." << name << " p50=" << q(0.50) << " p95=" << q(0.95) << " p99=" << q(0.99)
              << " max=" << values.back() << " mean=" << mean / (double)values.size() << "\n";
}

static double NowUs(std::chrono::steady_clock::time_point start) {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0;
}

static void DispatchLikeClient(WasmAddonManager& manager, const std::string& name, EventPayloadWriter&& writer) {
    if (!manager.HasSubscriber(name)) return;
    manager.DispatchEvent(name, writer.Finish());
}

static EventPayloadWriter CombatPayload(int32_t seed) {
    EventPayloadWriter writer("COMBAT_LOG_EVENT_UNFILTERED", 0);
    writer.Number(1234567.0 + seed);
    writer.String("SPELL_DAMAGE", 12);
    writer.String("0x0000000000001234", 18);
    writer.String("Testplayer", 10);
    writer.Number(1297);
    writer.String("0x0000000000005678", 18);
    writer.String("Testtarget", 10);
    writer.Number(1300);
    writer.String("Fireball", 8);
    writer.Number(4200);
    writer.Number(0);
    writer.Number(1);
    writer.Nil();
    writer.Nil();
    writer.Number(0);
    return writer;
}

static fs::path MakeScalePackages(const fs::path& dist, const fs::path& root, int count) {
    fs::path scaleRoot = root / "scale";
    fs::remove_all(scaleRoot);
    for (int i = 0; i < count; i++) {
        fs::path dir = scaleRoot / std::to_string(i);
        fs::create_directories(dir);
        std::error_code ec;
        fs::copy_file(dist / "bench-scale/addon.wasm", dir / "addon.wasm", ec);
        std::ofstream(dir / "addon.toml") << "[addon]\nid = \"bench.scale." << i << "\"\nname = \"Bench Scale\"\nversion = \"0.1.0\"\napi_version = \"2\"\n[permissions]\ncombat.read = true\n";
    }
    return scaleRoot;
}

static int RunColdChild(const char* pkg) {
    SectionHost host;
    WasmAddonManager manager;
    auto t0 = std::chrono::steady_clock::now();
    manager.Initialize(fs::temp_directory_path().string(), &host);
    double engineUs = NowUs(t0);
    t0 = std::chrono::steady_clock::now();
    bool ok = manager.LoadAddon(pkg);
    double loadUs = NowUs(t0);
    std::cout << "cold engine_us=" << engineUs << " first_load_us=" << loadUs << " ok=" << ok << "\n";
    manager.Shutdown();
    return ok ? 0 : 1;
}

static void RunStartup(const fs::path& dist, const char* self) {
    std::vector<double> engineSamples, loadSamples;
#if !defined(_WIN32)
    for (int i = 0; i < 10; i++) {
        std::string pkg = (dist / "bench-scale").string();
        char* args[] = {const_cast<char*>(self), const_cast<char*>("--cold-child"), const_cast<char*>(pkg.c_str()), nullptr};
        pid_t pid = 0;
        if (posix_spawn(&pid, self, nullptr, nullptr, args, environ) != 0) continue;
        int status = 0;
        waitpid(pid, &status, 0);
    }
    std::cout << "bench.startup.cold=measured-via-child (see cold lines above)\n";
#else
    std::cout << "bench.startup.cold=skipped\n";
#endif
    (void)engineSamples; (void)loadSamples;
    SectionHost host;
    WasmAddonManager manager;
    fs::path root = fs::temp_directory_path() / "wasm_bench_startup";
    fs::remove_all(root);
    fs::create_directories(root);
    manager.Initialize(root.string(), &host);
    std::string pkg = (dist / "bench-scale").string();
    std::vector<double> warm;
    for (int i = 0; i < 100; i++) {
        auto t0 = std::chrono::steady_clock::now();
        manager.LoadAddon(pkg);
        warm.push_back(NowUs(t0));
        manager.UnloadAddon("bench.scale");
    }
    PrintStats("startup.warm.load_us", warm);
    // ʕ ◕ᴥ◕ ʔ✿ Phase breakdown exposes the validate+instantiate double compile ✿ ʕ ◕ᴥ◕ ʔ
    auto runtime = CreateRuntime();
    std::vector<uint8_t> wasmBytes;
    {
        std::ifstream in(dist / "bench-scale/addon.wasm", std::ios::binary);
        wasmBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<double> validateUs, instantiateUs;
    Limits limits;
    for (int i = 0; i < 50; i++) {
        RuntimeInfo info; std::string error;
        auto t0 = std::chrono::steady_clock::now();
        runtime->Validate(wasmBytes, "bench", info, error);
        validateUs.push_back(NowUs(t0));
        t0 = std::chrono::steady_clock::now();
        auto instance = runtime->Instantiate(wasmBytes, "bench", limits, [](IWasmInstance&, HostFn, const std::vector<int64_t>&) { return (int64_t)0; }, error);
        instantiateUs.push_back(NowUs(t0));
    }
    PrintStats("startup.phase.validate_us", validateUs);
    PrintStats("startup.phase.instantiate_us", instantiateUs);
    manager.Shutdown();
}

static void RunScale(const fs::path& dist, int frames) {
    for (int count : {0, 10, 50, 100, 300}) {
        SectionHost host;
        WasmAddonManager manager;
        fs::path root = fs::temp_directory_path() / ("wasm_bench_scale_" + std::to_string(count));
        fs::remove_all(root);
        fs::create_directories(root);
        fs::path scaleRoot = MakeScalePackages(dist, root, count);
        manager.Initialize(scaleRoot.string(), &host);
        auto t0 = std::chrono::steady_clock::now();
        size_t loaded = count ? manager.ScanAndLoadAll() : 0;
        double loadMs = NowUs(t0) / 1000.0;
        int64_t out = 0;
        uint64_t wasmMem = 0;
        for (auto& info : manager.GetInspector()) {
            wasmMem += info.stats.memoryBytes;
            manager.SetPermissionOverride(info.manifest.id, PermCombatRead, true);
            manager.CallExportIntArg(info.manifest.id, "wow_set_mode", 1, out);
        }
        std::cout << "bench.scale." << count << " loaded=" << loaded << " load_total_ms=" << loadMs
                  << " wasm_mem_mb=" << (double)wasmMem / 1048576.0 << "\n";
        for (int eventsPerFrame : {0, 5, 25, 100}) {
            std::vector<double> frameTimes;
            for (int frame = 0; frame < frames; frame++) {
                auto ft0 = std::chrono::steady_clock::now();
                for (int k = 0; k < eventsPerFrame; k++)
                    DispatchLikeClient(manager, "COMBAT_LOG_EVENT_UNFILTERED", CombatPayload(k));
                EventPayloadWriter unit("UNIT_HEALTH", 0);
                unit.String("player", 6);
                DispatchLikeClient(manager, "UNIT_HEALTH", std::move(unit));
                frameTimes.push_back(NowUs(ft0));
            }
            std::string name = "scale." + std::to_string(count) + ".k" + std::to_string(eventsPerFrame) + ".frame_us";
            PrintStats(name.c_str(), frameTimes);
        }
        manager.Shutdown();
    }
}

static void RunCombat(const fs::path& dist) {
    std::vector<double> serializeUs;
    EventPayloadWriter probe = CombatPayload(0);
    std::vector<uint8_t> payload = probe.Finish();
    for (int i = 0; i < 1000; i++) {
        auto t0 = std::chrono::steady_clock::now();
        EventPayloadWriter writer = CombatPayload(i);
        std::vector<uint8_t> bytes = writer.Finish();
        serializeUs.push_back(NowUs(t0));
        if (i == 0) payload = bytes;
    }
    PrintStats("combat.serialize_us", serializeUs);
    std::cout << "bench.combat.payload_bytes=" << payload.size() << "\n";
    SectionHost host;
    WasmAddonManager manager;
    fs::path root = fs::temp_directory_path() / "wasm_bench_combat";
    fs::remove_all(root);
    fs::create_directories(root);
    manager.Initialize(root.string(), &host);
    manager.LoadAddon((dist / "bench-scale").string());
    int64_t out = 0;
    manager.CallExportIntArg("bench.scale", "wow_set_mode", 0, out);
    std::vector<double> granted, redacted;
    for (int i = 0; i < 200; i++) {
        auto t0 = std::chrono::steady_clock::now();
        manager.DispatchEvent("COMBAT_LOG_EVENT_UNFILTERED", CombatPayload(i).Finish());
        redacted.push_back(NowUs(t0));
    }
    manager.SetPermissionOverride("bench.scale", PermCombatRead, true);
    for (int i = 0; i < 200; i++) {
        auto t0 = std::chrono::steady_clock::now();
        manager.DispatchEvent("COMBAT_LOG_EVENT_UNFILTERED", CombatPayload(i).Finish());
        granted.push_back(NowUs(t0));
    }
    PrintStats("combat.v2.redacted_dispatch_us", redacted);
    PrintStats("combat.v2.granted_mode0_dispatch_us", granted);
    manager.Shutdown();
}


static uint64_t g_luaPings = 0;
static int LuaPing(lua_State*) { g_luaPings++; return 0; }

static double PerOpUs(std::chrono::steady_clock::time_point start, int64_t n) {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count() / 1000.0 / (double)n;
}

int main(int argc, char** argv) {
    std::string section;
    int frames = 120;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--cold-child" && i + 1 < argc) return RunColdChild(argv[i + 1]);
        if (arg == "--section" && i + 1 < argc) section = argv[++i];
        if (arg == "--frames" && i + 1 < argc) frames = std::stoi(argv[++i]);
    }
    if (!section.empty()) {
        fs::path dist = argc > 1 && argv[1][0] != '-' ? argv[1] : "Addons/Wasm/dist";
        if (section == "startup" || section == "all") RunStartup(dist, argv[0]);
        if (section == "scale" || section == "all") RunScale(dist, frames);
        if (section == "combat" || section == "all") RunCombat(dist);
        return 0;
    }
    fs::path dist = argc > 1 ? argv[1] : "Addons/Wasm/dist";
    int64_t n = argc > 2 ? std::stoll(argv[2]) : 200000;
    BenchHost host;
    WasmAddonManager manager;
    fs::path root = fs::temp_directory_path() / "wasm_addon_bench";
    fs::remove_all(root);
    fs::create_directories(root);
    manager.Initialize(root.string(), &host);

    volatile int64_t sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int64_t i = 0; i < n; i++) sink += i & 1;
    double nativeUs = PerOpUs(t0, n);

    if (!manager.LoadAddon((dist / "ping").string())) { std::cerr << "ping addon failed to load\n"; return 1; }
    int64_t out = 0;
    t0 = std::chrono::steady_clock::now();
    bool okEmpty = manager.CallExportIntArg("test.ping", "wow_bench_empty", n, out);
    double wasmEmptyUs = PerOpUs(t0, n);
    t0 = std::chrono::steady_clock::now();
    bool okPing = true;
    for (int64_t done = 0; done < n && okPing; done += 5000)
        okPing = manager.CallExportIntArg("test.ping", "wow_bench_ping", std::min<int64_t>(5000, n - done), out);
    double wasmPingUs = PerOpUs(t0, n);
    auto info = manager.GetInspector();
    manager.Shutdown();
    if (!okEmpty || !okPing) { std::cerr << "wasm bench exports failed\n"; return 1; }

    lua_State* L = lua_open();
    luaL_openlibs(L);
    lua_register(L, "ping", LuaPing);
    luaL_dostring(L, "function bench_empty(n) local a=0 for i=1,n do a=a+(i%2) end return a end");
    luaL_dostring(L, "function bench_ping(n) for i=1,n do ping() end end");
    auto luaCall = [&](const char* fn) {
        lua_getglobal(L, fn);
        lua_pushnumber(L, (lua_Number)n);
        auto start = std::chrono::steady_clock::now();
        lua_call(L, 1, 0);
        return PerOpUs(start, n);
    };
    double luaEmptyUs = luaCall("bench_empty");
    double luaPingUs = luaCall("bench_ping");
    lua_close(L);

    std::cout << "iterations=" << n << " sink=" << sink << " luaPings=" << g_luaPings << "\n";
    std::cout << "native_empty_loop us/op: " << nativeUs << "\n";
    std::cout << "lua51_empty_loop  us/op: " << luaEmptyUs << "\n";
    std::cout << "lua51_host_ping   us/op: " << luaPingUs << "\n";
    std::cout << "wasm_empty_loop   us/op: " << wasmEmptyUs << "\n";
    std::cout << "wasm_host_ping    us/op: " << wasmPingUs << "\n";
    if (!info.empty()) std::cout << "wasm addon memory bytes: " << info[0].stats.memoryBytes << " fuel used: " << info[0].stats.fuelUsed << "\n";
    return 0;
}
