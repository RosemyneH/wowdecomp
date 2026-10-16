#pragma once
#include "Engine/Source/WasmAddon/WasmTypes.h"

namespace WasmAddon {

bool ParseManifest(const std::string& text, Manifest& out, std::string& error);
bool IsValidAddonId(const std::string& id);
bool IsPlainFileName(const std::string& name);
std::string Sha256Hex(const uint8_t* data, size_t len);
std::string SanitizeDisplayText(const std::string& text);

} // namespace WasmAddon
