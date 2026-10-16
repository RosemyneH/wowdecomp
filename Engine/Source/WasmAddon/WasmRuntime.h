#pragma once
#include "Engine/Source/WasmAddon/WasmTypes.h"
#include <functional>
#include <memory>

namespace WasmAddon {

class IWasmInstance {
public:
    virtual ~IWasmInstance() = default;
    virtual bool ReadMemory(uint32_t ptr, uint32_t len, std::string& out) const = 0;
    virtual bool WriteMemory(uint32_t ptr, const void* data, uint32_t len) = 0;
    virtual uint64_t MemoryBytes() const = 0;
    virtual bool Call(const char* exportName, const std::vector<int64_t>& args, int64_t& result, std::string& error) = 0;
    virtual bool HasExport(const char* exportName) const = 0;
    virtual uint64_t ConsumedFuel() const = 0;
};

struct RuntimeInfo {
    std::vector<std::string> imports;
    std::vector<std::string> prohibitedImports;
};

using HostDispatchFn = std::function<int64_t(IWasmInstance& instance, HostFn fn, const std::vector<int64_t>& args)>;

class IWasmRuntime {
public:
    virtual ~IWasmRuntime() = default;
    virtual bool Validate(const std::vector<uint8_t>& module, const std::string& sha256, RuntimeInfo& info, std::string& error) = 0;
    virtual size_t ModuleCacheEntries() const { return 0; }
    virtual std::unique_ptr<IWasmInstance> Instantiate(const std::vector<uint8_t>& module, const std::string& sha256, const Limits& limits,
                                                       HostDispatchFn dispatch, std::string& error) = 0;
};

std::unique_ptr<IWasmRuntime> CreateRuntime();
bool RuntimeAvailable();

} // namespace WasmAddon
