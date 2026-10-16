#include "Engine/Source/WasmAddon/WasmManifest.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace WasmAddon {

const char* ToString(AddonState s) {
    switch (s) {
        case AddonState::Discovered: return "Discovered";
        case AddonState::Validated: return "Validated";
        case AddonState::Loaded: return "Loaded";
        case AddonState::Running: return "Running";
        case AddonState::Suspended: return "Suspended";
        case AddonState::Failed: return "Failed";
        case AddonState::Unloaded: return "Unloaded";
    }
    return "Unknown";
}

struct PermEntry { const char* name; Permission perm; bool sensitive; };
static const PermEntry kPerms[] = {
    {"inventory.read", PermInventoryRead, false}, {"units.read", PermUnitsRead, true},
    {"ui.create", PermUiCreate, false}, {"chat.print", PermChatPrint, false},
    {"chat.read", PermChatRead, true}, {"chat.send", PermChatSend, true},
    {"auction.read", PermAuctionRead, true}, {"auction.write", PermAuctionWrite, true},
    {"storage.read", PermStorageRead, true}, {"storage.write", PermStorageWrite, true},
    {"network.access", PermNetworkAccess, true}, {"filesystem.access", PermFilesystemAccess, true},
    {"process.execute", PermProcessExecute, true}, {"profiling.read", PermProfilingRead, false},
    {"combat.read", PermCombatRead, true},
};
const char* PermissionName(Permission p) {
    for (auto& e : kPerms) if (e.perm == p) return e.name;
    return "unknown";
}
bool PermissionFromName(const std::string& n, Permission& out) {
    for (auto& e : kPerms) if (n == e.name) { out = e.perm; return true; }
    return false;
}
bool IsSensitive(Permission p) {
    for (auto& e : kPerms) if (e.perm == p) return e.sensitive;
    return true;
}
std::vector<Permission> AllPermissions() {
    std::vector<Permission> v;
    for (auto& e : kPerms) v.push_back(e.perm);
    return v;
}

std::string SanitizeDisplayText(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '|') out += '/';
        else if ((unsigned char)c >= 0x20) out += c;
        else out += ' ';
    }
    return out;
}

std::string SanitizeUiText(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '|') out += '/';
        else if (c == '\n' || (unsigned char)c >= 0x20) out += c;
        else out += ' ';
    }
    return out;
}

bool IsValidAddonId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id)
        if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '.' || c == '-' || c == '_')) return false;
    if (id.front() == '.' || id.back() == '.') return false;
    // ʕ ●ᴥ●ʔ✿ Windows device names are not usable as storage file names ✿ ʕ ●ᴥ●ʔ
    static const char* kReserved[] = {"con","prn","aux","nul","com1","com2","com3","com4","com5","com6","com7","com8","com9","lpt1","lpt2","lpt3","lpt4","lpt5","lpt6","lpt7","lpt8","lpt9"};
    std::string base = id.substr(0, id.find('.'));
    for (auto reserved : kReserved) if (base == reserved) return false;
    return true;
}
bool IsPlainFileName(const std::string& n) {
    if (n.empty() || n.size() > 128) return false;
    // ʕ ●ᴥ●ʔ✿ Reject separators, parent refs, drive letters and ADS colons ✿ ʕ ●ᴥ●ʔ
    if (n.find_first_of("/\\:") != std::string::npos || n.find("..") != std::string::npos) return false;
    return true;
}
static bool ParseU64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    uint64_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        if (value > (UINT64_MAX - 9) / 10) return false;
        value = value * 10 + (uint64_t)(c - '0');
    }
    out = value;
    return true;
}

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
static std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

bool ParseManifest(const std::string& text, Manifest& out, std::string& error) {
    if (text.size() > kMaxManifestBytes) { error = "manifest exceeds size limit"; return false; }
    out = Manifest{};
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = Trim(text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos));
        pos = eol == std::string::npos ? text.size() : eol + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[') {
            if (line.back() != ']') { error = "malformed section: " + line; return false; }
            section = line.substr(1, line.size() - 2);
            if (section != "addon" && section != "permissions" && section != "limits") { error = "unknown section: " + section; return false; }
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) { error = "malformed line: " + line; return false; }
        std::string key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (section == "addon") {
            value = Unquote(value);
            if (key == "id") out.id = value;
            else if (key == "name") out.name = value;
            else if (key == "version") out.version = value;
            else if (key == "author") out.author = value;
            else if (key == "repository") out.repository = value;
            else if (key == "module") out.moduleFile = value;
            else if (key == "api_version") {
                uint64_t v;
                if (!ParseU64(value, v) || v > 1000) { error = "bad api_version"; return false; }
                out.apiVersion = (uint32_t)v;
            } else { error = "unknown addon key: " + key; return false; }
        } else if (section == "permissions") {
            Permission p;
            if (!PermissionFromName(key, p)) { error = "unknown permission: " + key; return false; }
            if (value == "true") out.declaredPermissions |= p;
            else if (value != "false") { error = "permission value must be true/false: " + key; return false; }
        } else {
            uint64_t v;
            if (!ParseU64(value, v)) { error = "bad limit: " + key; return false; }
            if (key == "memory_mb") out.requestedLimits.memoryBytes = std::min(std::min(v, kHostMaxMemoryBytes / (1024 * 1024)) * 1024 * 1024, kHostMaxMemoryBytes);
            else if (key == "fuel") out.requestedLimits.fuelPerCallback = v == 0 ? kDefaultFuelPerCallback : std::min(v, kHostMaxFuelPerCallback);
            else if (key == "storage_kb") out.requestedLimits.storageQuotaBytes = std::min(v * 1024, kHostMaxStorageQuotaBytes);
            else if (key == "max_ui_handles") out.requestedLimits.maxUiHandles = (uint32_t)std::min<uint64_t>(v, kMaxUiHandlesPerAddon);
            else if (key == "max_events") out.requestedLimits.maxEvents = (uint32_t)std::min<uint64_t>(v, kMaxEventsPerAddon);
            else { error = "unknown limit: " + key; return false; }
        }
    }
    if (!IsValidAddonId(out.id)) { error = "missing/invalid addon id"; return false; }
    if (out.name.empty() || out.version.empty()) { error = "missing name/version"; return false; }
    if (out.apiVersion < kApiVersionMin || out.apiVersion > kApiVersionMax) { error = "unsupported api_version"; return false; }
    if (!IsPlainFileName(out.moduleFile)) { error = "module must be a plain file name"; return false; }
    return true;
}

// ʕ ●ᴥ●ʔ✿ Compact SHA-256 (FIPS 180-4) for module identity; input is untrusted, length-checked by caller ✿ ʕ ●ᴥ●ʔ
namespace {
struct Sha256 {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    static uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Block(const uint8_t* p) {
        static const uint32_t K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (p[i*4] << 24) | (p[i*4+1] << 16) | (p[i*4+2] << 8) | p[i*4+3];
        for (int i = 16; i < 64; i++) { uint32_t s0 = R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3); uint32_t s1 = R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10); w[i] = w[i-16]+s0+w[i-7]+s1; }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1=R(e,6)^R(e,11)^R(e,25), ch=(e&f)^(~e&g), t1=hh+S1+ch+K[i]+w[i];
            uint32_t S0=R(a,2)^R(a,13)^R(a,22), mj=(a&b)^(a&c)^(b&c), t2=S0+mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
};
} // namespace

std::string Sha256Hex(const uint8_t* data, size_t len) {
    Sha256 s;
    size_t pos = 0;
    while (pos + 64 <= len) { s.Block(data + pos); pos += 64; }
    std::array<uint8_t, 128> tail{};
    size_t rem = len - pos;
    std::memcpy(tail.data(), data + pos, rem);
    tail[rem] = 0x80;
    size_t tailLen = rem < 56 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tailLen - 1 - i] = (uint8_t)(bits >> (i * 8));
    s.Block(tail.data());
    if (tailLen == 128) s.Block(tail.data() + 64);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (uint32_t v : s.h) for (int i = 28; i >= 0; i -= 4) out += hex[(v >> i) & 0xf];
    return out;
}

} // namespace WasmAddon
