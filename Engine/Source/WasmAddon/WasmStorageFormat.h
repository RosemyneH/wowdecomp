#pragma once
#include "Engine/Source/WasmAddon/WasmTypes.h"
#include <map>

namespace WasmAddon {

// ʕ •ᴥ•ʔ✿ Pure format functions (no I/O) so fuzzers and tests can drive them directly ✿ ʕ •ᴥ•ʔ
bool ParseStorageFile(const uint8_t* data, size_t len, int scope, std::map<std::string, std::vector<uint8_t>>& out);
std::vector<uint8_t> SerializeStorageFile(int scope, const std::map<std::string, std::vector<uint8_t>>& kv);
void ParsePermissionStore(const std::string& text, std::map<std::string, bool>& out);
std::string PermissionKey(const std::string& id, const std::string& sha256, Permission perm);
uint32_t EffectiveGrants(const std::map<std::string, bool>& store, const Manifest& manifest, const std::string& sha256);
std::string SerializePermissionStore(const std::map<std::string, bool>& store);

} // namespace WasmAddon
