#include "Engine/Source/WasmAddon/WasmAddonManager.h"
#include <iostream>

using namespace WasmAddon;

struct ConsoleHost final : IWasmHostServices {
    std::vector<std::string> chat;
    std::map<uint64_t, std::string> ui;
    uint64_t nextUi = 1;
    bool ChatPrint(const std::string&, const std::string& text) override {
        chat.push_back(text);
        std::cout << "[chat] " << text << "\n";
        return true;
    }
    bool GetInventorySnapshot(std::vector<InventoryItem>& out) override {
        out = {{0, 1, 6948, 1}, {0, 2, 2589, 20}, {1, 1, 117, 4}};
        return true;
    }
    uint64_t UiCreate(const std::string&, const std::string& kind) override {
        uint64_t handle = nextUi++;
        ui[handle] = kind;
        std::cout << "[ui] create " << kind << " -> " << handle << "\n";
        return handle;
    }
    uint64_t UiSpawn(const std::string&, uint64_t parent, const std::string& kind) override {
        uint64_t handle = nextUi++;
        ui[handle] = kind;
        std::cout << "[ui] spawn " << kind << " parent=" << parent << " -> " << handle << "\n";
        return handle;
    }
    bool UiStyle(uint64_t handle, int32_t op, const uint8_t*, uint32_t len) override {
        if (!ui.count(handle)) return false;
        std::cout << "[ui] style " << handle << " op=" << op << " bytes=" << len << "\n";
        return true;
    }
    bool UiSetText(uint64_t handle, const std::string& text) override {
        if (!ui.count(handle)) return false;
        std::cout << "[ui] set_text " << handle << " = " << text << "\n";
        return true;
    }
    bool UiBind(uint64_t handle, int32_t guest, const std::string& event) override {
        if (!ui.count(handle)) return false;
        std::cout << "[ui] bind " << handle << " guest=" << guest << " event=" << event << "\n";
        return true;
    }
    bool UiGetText(uint64_t handle, std::string& out) override {
        if (!ui.count(handle)) return false;
        out.clear();
        std::cout << "[ui] get_text " << handle << "\n";
        return true;
    }
    int ExecuteLua(const std::string&, const std::string&, const std::string& source, std::string&) override {
        std::cout << "[lua] exec " << source.size() << " bytes\n";
        return 0;
    }
    int LoadLuaPath(const std::string&, const std::string&, const std::string& path, std::string&) override {
        std::cout << "[lua] load " << path << "\n";
        return 0;
    }
    int LoadNamedAddon(const std::string& name, std::string&) override {
        std::cout << "[lua] addon " << name << "\n";
        return 0;
    }
    int CallLua(const std::string&, const uint8_t*, uint32_t, std::vector<uint8_t>& out, std::string&) override {
        std::cout << "[lua] call\n";
        out = {1, 0, 4, 1, 0, 0, 0};
        return 0;
    }
    int BindLua(const std::string&, int32_t handle, const std::string& script, std::string&) override {
        std::cout << "[lua] bind " << handle << " " << script << "\n";
        return 0;
    }
    bool GetUnitNames(const std::vector<std::string>& tokens, std::vector<std::pair<uint32_t, std::string>>& out) override {
        out.clear();
        for (size_t i = 0; i < tokens.size(); i++) out.emplace_back((uint32_t)i, "Unit" + std::to_string(i));
        std::cout << "[unit] names " << tokens.size() << "\n";
        return true;
    }
    void UiDestroy(uint64_t handle) override { ui.erase(handle); }
    void Log(const std::string& message) override { std::cout << message << "\n"; }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wasm_addon_host <addon-dir|addons-root> [--scan] [--event NAME]... [--inspector]\n";
        return 2;
    }
    ConsoleHost host;
    WasmAddonManager manager;
    manager.Initialize(argv[1], &host);
    bool scan = false, inspector = false;
    std::vector<std::string> events;
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--scan") scan = true;
        else if (arg == "--inspector") inspector = true;
        else if (arg == "--event" && i + 1 < argc) events.push_back(argv[++i]);
    }
    size_t loaded = scan ? manager.ScanAndLoadAll() : (manager.LoadAddon(argv[1]) ? 1 : 0);
    std::cout << "loaded=" << loaded << "\n";
    for (auto& event : events) manager.DispatchEvent(event);
    if (inspector) std::cout << manager.FormatInspectorText();
    int failures = 0;
    for (auto& info : manager.GetInspector())
        if (info.state == AddonState::Failed) failures++;
    manager.Shutdown();
    return failures && !loaded ? 1 : 0;
}
