#include "Engine/Source/WasmAddon/WasmManifest.h"
#include "Engine/Source/WasmAddon/WasmStorageFormat.h"
#include <cstdio>
#include <cstring>
#include <random>

using namespace WasmAddon;

static bool CheckParse(std::vector<uint8_t> data, int scope) {
    std::map<std::string, std::vector<uint8_t>> out = {{"sentinel", {1}}};
    bool ok = ParseStorageFile(data.data(), data.size(), scope, out);
    if (!ok) {
        if (out.size() != 1 || !out.count("sentinel")) { std::fprintf(stderr, "FAIL: output touched on rejected parse\n"); return false; }
        return true;
    }
    for (auto& [key, value] : out) {
        if (key.empty() || key.size() > kMaxStorageKeyBytes || value.size() > kMaxStorageValueBytes) {
            std::fprintf(stderr, "FAIL: limit violated after successful parse\n");
            return false;
        }
    }
    if (out.size() > kMaxStorageKeysPerScope) { std::fprintf(stderr, "FAIL: key count limit violated\n"); return false; }
    std::vector<uint8_t> roundTrip = SerializeStorageFile(scope, out);
    std::map<std::string, std::vector<uint8_t>> again;
    if (!ParseStorageFile(roundTrip.data(), roundTrip.size(), scope, again) || again != out) {
        std::fprintf(stderr, "FAIL: round trip mismatch\n");
        return false;
    }
    return true;
}

static void FixTrailer(std::vector<uint8_t>& data) {
    if (data.size() < 32) return;
    std::string digest = Sha256Hex(data.data(), data.size() - 32);
    std::memcpy(data.data() + data.size() - 32, digest.data(), 32);
}

int main(int argc, char** argv) {
    int iterations = 20000;
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) iterations = std::atoi(argv[++i]);

    std::vector<std::vector<uint8_t>> seeds;
    seeds.push_back(SerializeStorageFile(0, {{"greeting", {'h', 'e', 'l', 'l', 'o'}}}));
    seeds.push_back(SerializeStorageFile(1, {{"char-key", {'x'}}}));
    seeds.push_back(SerializeStorageFile(0, {}));
    {
        std::map<std::string, std::vector<uint8_t>> many;
        for (int i = 0; i < 64; i++) many["k" + std::to_string(i)] = {(uint8_t)i};
        seeds.push_back(SerializeStorageFile(0, many));
    }

    std::mt19937 rng(335);
    for (int iter = 0; iter < iterations; iter++) {
        std::vector<uint8_t> data = seeds[rng() % seeds.size()];
        int mutations = 1 + (int)(rng() % 16);
        for (int m = 0; m < mutations; m++) {
            switch (rng() % 4) {
                case 0:
                    if (!data.empty()) data[rng() % data.size()] = (uint8_t)rng();
                    break;
                case 1:
                    if (data.size() > 1) data.resize(rng() % data.size());
                    break;
                case 2:
                    data.insert(data.begin() + (data.empty() ? 0 : rng() % (data.size() + 1)), (uint8_t)rng());
                    break;
                default: {
                    static const uint32_t kBounds[] = {0, 1, 127, 128, 129, 65535, 65536, 0xFFFFFFFFu};
                    if (data.size() > 45) {
                        size_t at = 13 + rng() % (data.size() - 45 < 20 ? 1 : 20);
                        uint32_t v = kBounds[rng() % 8];
                        for (int b = 0; b < 4 && at + b < data.size(); b++) data[at + b] = (uint8_t)(v >> (b * 8));
                    }
                    break;
                }
            }
        }
        if (!CheckParse(data, (int)(rng() % 2))) return 1;
        FixTrailer(data);
        if (!CheckParse(data, (int)(rng() % 2))) return 1;
    }
    std::printf("storage fuzz: %d iterations, no violations\n", iterations);

    // ʕ ●ᴥ●ʔ✿ Grant invariants: declared-only, hash-bound, sensitive exact-match, monotone deny ✿ ʕ ●ᴥ●ʔ
    const char* kIds[] = {"test.storage", "example.hello", "evil.addon"};
    const std::string shaA(64, 'a'), shaB(64, 'b');
    for (int iter = 0; iter < 5000; iter++) {
        Manifest manifest;
        manifest.id = kIds[rng() % 3];
        manifest.declaredPermissions = (uint32_t)(rng() & 0x7fff);
        std::map<std::string, bool> store;
        for (int e = 0, entries = (int)(rng() % 8); e < entries; e++) {
            Permission perm = AllPermissions()[rng() % AllPermissions().size()];
            const std::string& sha = (rng() % 3 == 0) ? shaB : shaA;
            store[PermissionKey(kIds[rng() % 3], sha, perm)] = (rng() % 2) != 0;
            if (rng() % 4 == 0) store["junk|" + std::to_string(rng())] = true;
        }
        uint32_t grants = EffectiveGrants(store, manifest, shaA);
        if (grants & ~manifest.declaredPermissions) { std::fprintf(stderr, "FAIL: grant beyond declared\n"); return 1; }
        for (Permission perm : AllPermissions()) {
            if (!(manifest.declaredPermissions & perm) || !IsSensitive(perm)) continue;
            bool expected = false;
            auto it = store.find(PermissionKey(manifest.id, shaA, perm));
            if (it != store.end()) expected = it->second;
            if (((grants & perm) != 0) != expected) { std::fprintf(stderr, "FAIL: sensitive exact-match\n"); return 1; }
        }
        std::map<std::string, bool> denied = store;
        for (auto& [key, allow] : denied) if (rng() % 2) allow = false;
        if (EffectiveGrants(denied, manifest, shaA) & ~grants) { std::fprintf(stderr, "FAIL: monotone deny\n"); return 1; }
    }
    std::printf("grants fuzz: 5000 iterations, no violations\n");
    return 0;
}
