#include "Engine/Source/WasmAddon/WasmStorageFormat.h"
#include "Engine/Source/WasmAddon/WasmManifest.h"
#include <algorithm>
#include <cstring>

namespace WasmAddon {
namespace {
void PutU16(std::vector<uint8_t>& out, uint16_t v) { out.push_back((uint8_t)v); out.push_back((uint8_t)(v >> 8)); }
void PutU32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (i * 8))); }
uint32_t GetU32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
} // namespace

bool ParseStorageFile(const uint8_t* data, size_t len, int scope, std::map<std::string, std::vector<uint8_t>>& out) {
    if (len < 13 + 32 || std::memcmp(data, "WKV1", 4) != 0) return false;
    if (GetU32(data + 4) != 1 || data[8] != (uint8_t)scope) return false;
    std::string digest = Sha256Hex(data, len - 32);
    if (std::memcmp(data + len - 32, digest.data(), 32) != 0) return false;
    uint32_t count = GetU32(data + 9);
    if (count > kMaxStorageKeysPerScope) return false;
    size_t pos = 13, end = len - 32;
    std::map<std::string, std::vector<uint8_t>> parsed;
    for (uint32_t i = 0; i < count; i++) {
        if (pos + 6 > end) return false;
        uint16_t keyLen = (uint16_t)(data[pos] | (data[pos + 1] << 8));
        uint32_t valLen = GetU32(data + pos + 2);
        pos += 6;
        if (keyLen == 0 || keyLen > kMaxStorageKeyBytes || valLen > kMaxStorageValueBytes || pos + keyLen + valLen > end) return false;
        std::string key((const char*)data + pos, keyLen);
        if (parsed.count(key)) return false;
        parsed[key] = std::vector<uint8_t>(data + pos + keyLen, data + pos + keyLen + valLen);
        pos += keyLen + valLen;
    }
    if (pos != end) return false;
    out = std::move(parsed);
    return true;
}

std::vector<uint8_t> SerializeStorageFile(int scope, const std::map<std::string, std::vector<uint8_t>>& kv) {
    std::vector<uint8_t> bytes = {'W', 'K', 'V', '1'};
    PutU32(bytes, 1);
    bytes.push_back((uint8_t)scope);
    PutU32(bytes, (uint32_t)kv.size());
    for (auto& [key, value] : kv) {
        PutU16(bytes, (uint16_t)key.size());
        PutU32(bytes, (uint32_t)value.size());
        bytes.insert(bytes.end(), key.begin(), key.end());
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    std::string digest = Sha256Hex(bytes.data(), bytes.size());
    bytes.insert(bytes.end(), digest.begin(), digest.begin() + 32);
    return bytes;
}

std::string PermissionKey(const std::string& id, const std::string& sha256, Permission perm) {
    return id + "|" + sha256 + "|" + PermissionName(perm);
}

uint32_t EffectiveGrants(const std::map<std::string, bool>& store, const Manifest& manifest, const std::string& sha256) {
    uint32_t granted = 0;
    for (Permission perm : AllPermissions()) {
        if (!(manifest.declaredPermissions & perm)) continue;
        auto it = store.find(PermissionKey(manifest.id, sha256, perm));
        bool allow = it != store.end() ? it->second : !IsSensitive(perm);
        if (allow) granted |= perm;
    }
    return granted;
}

void ParsePermissionStore(const std::string& text, std::map<std::string, bool>& out) {
    out.clear();
    // ʕ •ᴥ•ʔ✿ Entry cap, enforced after parsing so denies are the last to go ✿ ʕ •ᴥ•ʔ
    auto enforceCap = [&] {
        while (out.size() > 10000) {
            auto it = std::find_if(out.begin(), out.end(), [](const auto& e) { return e.second; });
            if (it == out.end()) break;
            out.erase(it);
        }
    };
    size_t linePos = 0;
    while (linePos <= text.size()) {
        size_t eol = text.find('\n', linePos);
        std::string line = text.substr(linePos, eol == std::string::npos ? eol : eol - linePos);
        linePos = eol == std::string::npos ? text.size() + 1 : eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::string fields[4];
        size_t fieldCount = 0, pos = 0;
        bool tooMany = false;
        while (true) {
            if (fieldCount == 4) { tooMany = true; break; }
            size_t tab = line.find('\t', pos);
            fields[fieldCount++] = line.substr(pos, tab == std::string::npos ? tab : tab - pos);
            if (tab == std::string::npos) break;
            pos = tab + 1;
        }
        // ʕ ●ᴥ●ʔ✿ Bail out early: a tab-heavy line must not allocate millions of strings ✿ ʕ ●ᴥ●ʔ
        if (tooMany || fieldCount != 4) continue;
        // ʕ ●ᴥ●ʔ✿ '|' is the key separator; a field containing it can never round-trip ✿ ʕ ●ᴥ●ʔ
        if (fields[0].find('|') != std::string::npos || fields[1].find('|') != std::string::npos || fields[2].find('|') != std::string::npos) continue;
        // ʕ •ᴥ•ʔ✿ Duplicate keys: deny wins, regardless of order ✿ ʕ •ᴥ•ʔ
        std::string key = fields[0] + "|" + fields[1] + "|" + fields[2];
        auto [it, inserted] = out.try_emplace(key, fields[3] == "allow");
        if (!inserted) it->second = it->second && fields[3] == "allow";
    }
    enforceCap();
}
std::string SerializePermissionStore(const std::map<std::string, bool>& store) {
    std::string out;
    for (auto& [key, allow] : store) {
        std::string copy = key;
        for (char& c : copy) if (c == '|') c = '\t';
        out += copy;
        out += '\t';
        out += allow ? "allow" : "deny";
        out += '\n';
    }
    return out;
}

} // namespace WasmAddon
