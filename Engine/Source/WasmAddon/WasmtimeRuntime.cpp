#include "Engine/Source/WasmAddon/WasmRuntime.h"

#if defined(WOWDECOMP_HAS_WASMTIME)
#include <wasmtime.h>
#include <cstring>
#include <deque>
#include <map>

namespace WasmAddon {
namespace {

std::string NameToString(const wasm_name_t* name) { return {name->data, name->size}; }
std::string ErrorText(wasmtime_error_t* error) {
    wasm_name_t msg;
    wasmtime_error_message(error, &msg);
    std::string out(msg.data, msg.size);
    wasm_byte_vec_delete(&msg);
    wasmtime_error_delete(error);
    return out;
}
std::string TrapText(wasm_trap_t* trap) {
    wasm_message_t msg;
    wasm_trap_message(trap, &msg);
    std::string out(msg.data, msg.size);
    wasm_byte_vec_delete(&msg);
    wasm_trap_delete(trap);
    return out;
}

class WasmtimeInstance;
struct Shared {
    HostDispatchFn dispatch;
    WasmtimeInstance* instance = nullptr;
};
struct HostEnv {
    Shared* shared;
    HostFn fn;
};

class WasmtimeInstance final : public IWasmInstance {
public:
    wasm_engine_t* engine = nullptr;
    wasmtime_store_t* store = nullptr;
    wasmtime_linker_t* linker = nullptr;
    wasmtime_module_t* module = nullptr;
    wasmtime_instance_t instance{};
    wasmtime_memory_t memory{};
    bool hasMemory = false;
    Limits limits;
    std::shared_ptr<Shared> shared;
    std::deque<HostEnv> hostEnvs;
    uint64_t fuelBudget = 0;
    mutable uint64_t lastConsumed = 0;

    ~WasmtimeInstance() override {
        if (linker) wasmtime_linker_delete(linker);
        if (module) wasmtime_module_delete(module);
        if (store) wasmtime_store_delete(store);
    }
    wasmtime_context_t* Context() const { return wasmtime_store_context(store); }

    bool ReadMemory(uint32_t ptr, uint32_t len, std::string& out) const override {
        if (!hasMemory) return false;
        const uint8_t* data = wasmtime_memory_data(Context(), &memory);
        size_t size = wasmtime_memory_data_size(Context(), &memory);
        if (!data || (uint64_t)ptr + len > size) return false;
        out.assign(reinterpret_cast<const char*>(data + ptr), len);
        return true;
    }
    bool WriteMemory(uint32_t ptr, const void* src, uint32_t len) override {
        if (!hasMemory) return false;
        uint8_t* data = wasmtime_memory_data(Context(), &memory);
        size_t size = wasmtime_memory_data_size(Context(), &memory);
        if (!data || (uint64_t)ptr + len > size) return false;
        std::memcpy(data + ptr, src, len);
        return true;
    }
    uint64_t MemoryBytes() const override {
        return hasMemory ? wasmtime_memory_data_size(Context(), &memory) : 0;
    }
    uint64_t ConsumedFuel() const override { return lastConsumed; }
    bool HasExport(const char* name) const override {
        wasmtime_extern_t item;
        return wasmtime_instance_export_get(Context(), &instance, name, std::strlen(name), &item);
    }

    bool Call(const char* name, const std::vector<int64_t>& args, int64_t& result, std::string& error) override {
        wasmtime_extern_t item;
        if (!wasmtime_instance_export_get(Context(), &instance, name, std::strlen(name), &item) || item.kind != WASMTIME_EXTERN_FUNC) {
            error = std::string("missing export: ") + name;
            return false;
        }
        std::vector<wasmtime_val_t> vals(args.size());
        for (size_t i = 0; i < args.size(); i++) { vals[i].kind = WASMTIME_I32; vals[i].of.i32 = (int32_t)args[i]; }
        // ʕ •ᴥ•ʔ✿ Honour the export's real arity (wow_free returns nothing) ✿ ʕ •ᴥ•ʔ
        wasm_functype_t* funcType = wasmtime_func_type(Context(), &item.of.func);
        if (funcType && wasm_functype_params(funcType)->size != args.size()) {
            wasm_functype_delete(funcType);
            error = "host arity mismatch calling " + std::string(name);
            return false;
        }
        size_t resultCount = funcType ? wasm_functype_results(funcType)->size : 1;
        std::vector<wasmtime_val_t> results(resultCount);
        for (auto& r : results) r.kind = WASMTIME_I32;
        if (funcType) wasm_functype_delete(funcType);
        wasmtime_context_set_fuel(Context(), limits.fuelPerCallback);
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* err = wasmtime_func_call(Context(), &item.of.func, vals.data(), vals.size(), results.data(), results.size(), &trap);
        uint64_t remaining = 0;
        if (wasmtime_context_get_fuel(Context(), &remaining) == nullptr)
            lastConsumed = limits.fuelPerCallback >= remaining ? limits.fuelPerCallback - remaining : 0;
        if (err) { error = ErrorText(err); return false; }
        if (trap) { error = TrapText(trap); return false; }
        result = results.empty() ? 0 : (results[0].kind == WASMTIME_I64 ? results[0].of.i64 : results[0].of.i32);
        return true;
    }
};

wasm_trap_t* HostCall(void* env, wasmtime_caller_t*, const wasmtime_val_t* args, size_t nargs,
                      wasmtime_val_t* results, size_t nresults) {
    auto* hostEnv = static_cast<HostEnv*>(env);
    if (!hostEnv->shared->instance || !hostEnv->shared->dispatch) return nullptr;
    std::vector<int64_t> values;
    for (size_t i = 0; i < nargs; i++)
        values.push_back(args[i].kind == WASMTIME_I64 ? args[i].of.i64 : args[i].of.i32);
    int64_t result = ErrGeneric;
    // ʕ ●ᴥ●ʔ✿ A C++ exception must never unwind through Wasmtime's C frames ✿ ʕ ●ᴥ●ʔ
    try { result = hostEnv->shared->dispatch(*hostEnv->shared->instance, hostEnv->fn, values); } catch (...) { result = ErrGeneric; }
    if (nresults) {
        if (hostEnv->fn == HostFn::PerfQuery || hostEnv->fn == HostFn::StorageUsage) { results[0].kind = WASMTIME_I64; results[0].of.i64 = result; }
        else { results[0].kind = WASMTIME_I32; results[0].of.i32 = (int32_t)result; }
    }
    return nullptr;
}

wasm_functype_t* FuncType(std::vector<wasm_valkind_t> params, std::vector<wasm_valkind_t> results) {
    wasm_valtype_vec_t p, r;
    std::vector<wasm_valtype_t*> pv, rv;
    for (auto k : params) pv.push_back(wasm_valtype_new(k));
    for (auto k : results) rv.push_back(wasm_valtype_new(k));
    wasm_valtype_vec_new(&p, pv.size(), pv.data());
    wasm_valtype_vec_new(&r, rv.size(), rv.data());
    return wasm_functype_new(&p, &r);
}

class WasmtimeRuntime final : public IWasmRuntime {
public:
    wasm_engine_t* engine = nullptr;
    // ʕ •ᴥ•ʔ✿ Compiled-module cache keyed by SHA-256: Validate compiles once, Instantiate clones ✿ ʕ •ᴥ•ʔ
    std::map<std::string, wasmtime_module_t*> moduleCache;
    WasmtimeRuntime() {
        wasm_config_t* config = wasm_config_new();
        wasmtime_config_consume_fuel_set(config, true);
        wasmtime_config_max_wasm_stack_set(config, 512 * 1024);
        // ʕ •ᴥ•ʔ✿ Pin the feature set: no threads, shared memory, memory64, multi-memory or GC ✿ ʕ •ᴥ•ʔ
        wasmtime_config_wasm_threads_set(config, false);
        wasmtime_config_wasm_multi_memory_set(config, false);
        wasmtime_config_wasm_memory64_set(config, false);
        wasmtime_config_wasm_gc_set(config, false);
        wasmtime_config_wasm_component_model_set(config, false);
        wasmtime_config_epoch_interruption_set(config, false);
        engine = wasm_engine_new_with_config(config);
    }
    ~WasmtimeRuntime() override {
        for (auto& [sha, module] : moduleCache) wasmtime_module_delete(module);
        if (engine) wasm_engine_delete(engine);
    }

    size_t ModuleCacheEntries() const override { return moduleCache.size(); }

    bool Validate(const std::vector<uint8_t>& bytes, const std::string& sha256, RuntimeInfo& info, std::string& error) override {
        wasmtime_module_t* module = nullptr;
        bool cached = false;
        auto it = moduleCache.find(sha256);
        if (it != moduleCache.end()) { module = it->second; cached = true; }
        if (!module) {
            wasmtime_error_t* err = wasmtime_module_new(engine, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &module);
            if (err) { error = ErrorText(err); return false; }
        }
        wasm_importtype_vec_t imports;
        wasmtime_module_imports(module, &imports);
        static const char* kKnown[] = {"api_version","ping","chat_print","event_subscribe","inventory_snapshot","ui_create","ui_set_text","perf_query","storage_get","storage_len","storage_keys","storage_usage","storage_set","storage_delete","unit_snapshot","auction_snapshot","chat_send_request","auction_bid_request"};
        for (size_t i = 0; i < imports.size; i++) {
            std::string mod = NameToString(wasm_importtype_module(imports.data[i]));
            std::string name = NameToString(wasm_importtype_name(imports.data[i]));
            std::string full = mod + "." + name;
            bool known = false;
            if (mod == "wow") for (auto k : kKnown) known = known || name == k;
            if (known) info.imports.push_back(full);
            else info.prohibitedImports.push_back(full);
        }
        wasm_importtype_vec_delete(&imports);
        if (!cached && info.prohibitedImports.empty() && !sha256.empty()) moduleCache[sha256] = module;
        else if (!cached && !info.prohibitedImports.empty()) wasmtime_module_delete(module);
        if (!info.prohibitedImports.empty()) {
            error = "prohibited imports:";
            for (auto& name : info.prohibitedImports) error += " " + name;
            return false;
        }
        return true;
    }

    std::unique_ptr<IWasmInstance> Instantiate(const std::vector<uint8_t>& bytes, const std::string& sha256, const Limits& limits,
                                               HostDispatchFn dispatch, std::string& error) override {
        auto inst = std::make_unique<WasmtimeInstance>();
        inst->engine = engine;
        inst->limits = limits;
        inst->shared = std::make_shared<Shared>();
        inst->shared->dispatch = std::move(dispatch);
        inst->shared->instance = inst.get();
        inst->store = wasmtime_store_new(engine, nullptr, nullptr);
        wasmtime_store_limiter(inst->store, (int64_t)limits.memoryBytes, 10000, 1, 1, 1);
        wasmtime_error_t* err = nullptr;
        auto cachedIt = moduleCache.find(sha256);
        if (cachedIt != moduleCache.end()) {
            inst->module = wasmtime_module_clone(cachedIt->second);
            wasmtime_module_delete(cachedIt->second);
            moduleCache.erase(cachedIt);
        } else {
            err = wasmtime_module_new(engine, bytes.data(), bytes.size(), &inst->module);
        }
        if (err) { error = ErrorText(err); return nullptr; }
        inst->linker = wasmtime_linker_new(engine);
        struct Def { HostFn fn; const char* name; std::vector<wasm_valkind_t> p, r; };
        const Def defs[] = {
            {HostFn::ApiVersion, "api_version", {}, {WASM_I32}},
            {HostFn::Ping, "ping", {}, {WASM_I32}},
            {HostFn::ChatPrint, "chat_print", {WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::EventSubscribe, "event_subscribe", {WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::InventorySnapshot, "inventory_snapshot", {WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::UiCreate, "ui_create", {WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::UiSetText, "ui_set_text", {WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::PerfQuery, "perf_query", {WASM_I32}, {WASM_I64}},
            {HostFn::StorageGet, "storage_get", {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::StorageLen, "storage_len", {WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::StorageKeys, "storage_keys", {WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::StorageUsage, "storage_usage", {WASM_I32}, {WASM_I64}},
            {HostFn::StorageSet, "storage_set", {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::StorageDelete, "storage_delete", {WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::UnitSnapshot, "unit_snapshot", {WASM_I32, WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::AuctionSnapshot, "auction_snapshot", {WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::ChatSendRequest, "chat_send_request", {WASM_I32, WASM_I32, WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
            {HostFn::AuctionBidRequest, "auction_bid_request", {WASM_I32, WASM_I32, WASM_I32}, {WASM_I32}},
        };
        // ʕ ◕ᴥ◕ ʔ✿ HostEnv objects live as long as the linker via finalizer-free storage owned by the instance ✿ ʕ ◕ᴥ◕ ʔ
        inst->hostEnvs.clear();
        for (const Def& def : defs) {
            inst->hostEnvs.push_back({inst->shared.get(), def.fn});
            HostEnv* env = &inst->hostEnvs.back();
            wasm_functype_t* type = FuncType(def.p, def.r);
            err = wasmtime_linker_define_func(inst->linker, "wow", 3, def.name, std::strlen(def.name), type, HostCall, env, nullptr);
            wasm_functype_delete(type);
            if (err) { error = ErrorText(err); return nullptr; }
        }
        wasm_trap_t* trap = nullptr;
        err = wasmtime_linker_instantiate(inst->linker, inst->Context(), inst->module, &inst->instance, &trap);
        if (err) { error = ErrorText(err); return nullptr; }
        if (trap) { error = TrapText(trap); return nullptr; }
        wasmtime_extern_t item;
        if (wasmtime_instance_export_get(inst->Context(), &inst->instance, "memory", 6, &item) && item.kind == WASMTIME_EXTERN_MEMORY) {
            inst->memory = item.of.memory;
            inst->hasMemory = true;
        }
        return inst;
    }
};

} // namespace

std::unique_ptr<IWasmRuntime> CreateRuntime() { return std::make_unique<WasmtimeRuntime>(); }
bool RuntimeAvailable() { return true; }

} // namespace WasmAddon

#else
namespace WasmAddon {
std::unique_ptr<IWasmRuntime> CreateRuntime() { return nullptr; }
bool RuntimeAvailable() { return false; }
} // namespace WasmAddon
#endif
