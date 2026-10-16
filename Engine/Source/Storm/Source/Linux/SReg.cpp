#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include "Engine/Source/Base/OsTypes.h"
#include "Engine/Source/Storm/H/storm.h"

#define SREG_TYPE_STRING 's'
#define SREG_TYPE_DWORD 'd'

struct REGVALUE {
    char type;
    std::string data;
};

typedef std::map<std::string, REGVALUE> REGSTORE;

static pthread_mutex_t s_regLock = PTHREAD_MUTEX_INITIALIZER;

int32_t STORMAPI SRegGetBaseKey(uint32_t flags, char* buffer, uint32_t bufferchars) {
    if (flags & SREG_BATTLE_NET) {
        SStrCopy(buffer, "Software\\Battle.net\\", bufferchars);
    } else {
        SStrCopy(buffer, "Software\\Blizzard Entertainment\\", bufferchars);
    }

    return 1;
}

static void BuildFullKeyName(const char* keyname, uint32_t flags, char* buffer, uint32_t bufferchars) {
    *buffer = 0;

    if (!(flags & SREG_ABSOLUTE)) {
        SRegGetBaseKey(flags, buffer, bufferchars);
    }

    SStrPack(buffer, keyname, bufferchars);
}

static void GetStorePath(char* buffer, uint32_t bufferchars, int32_t create) {
    const char* config = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");

    if (config && *config) {
        SStrCopy(buffer, config, bufferchars);
    } else {
        SStrCopy(buffer, home ? home : ".", bufferchars);
        SStrPack(buffer, "/.config", bufferchars);
    }

    SStrPack(buffer, "/Blizzard", bufferchars);

    if (create) {
        mkdir(buffer, 0755);
    }

    SStrPack(buffer, "/registry.txt", bufferchars);
}

static std::string MakeEntryName(const char* fullkeyname, const char* valuename) {
    std::string name(fullkeyname);
    name.push_back('\\');
    name.append(valuename);

    for (char& c : name) {
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
    }

    return name;
}

static void LoadStore(REGSTORE& store) {
    char path[1024];
    GetStorePath(path, sizeof(path), 0);

    FILE* file = fopen(path, "r");

    if (!file) {
        return;
    }

    char line[4096];

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\r\n")] = 0;

        char* type = strchr(line, '\t');

        if (!type || !type[1] || type[2] != '\t') {
            continue;
        }

        *type = 0;
        REGVALUE& value = store[line];
        value.type = type[1];
        value.data = &type[3];
    }

    fclose(file);
}

static int32_t SaveStore(const REGSTORE& store) {
    char path[1024];
    GetStorePath(path, sizeof(path), 1);

    FILE* file = fopen(path, "w");

    if (!file) {
        return 0;
    }

    for (const auto& entry : store) {
        fprintf(file, "%s\t%c\t%s\n", entry.first.c_str(), entry.second.type, entry.second.data.c_str());
    }

    return fclose(file) == 0;
}

static int32_t InternalLoadEntry(const char* keyname, const char* valuename, uint32_t flags, REGVALUE* value) {
    if ((flags & SREG_EXCLUDE_CURRENT_USER) && (flags & SREG_EXCLUDE_LOCAL_MACHINE)) {
        return 0;
    }

    char fullkeyname[MAX_PATH];
    BuildFullKeyName(keyname, flags, fullkeyname, MAX_PATH);

    REGSTORE store;
    pthread_mutex_lock(&s_regLock);
    LoadStore(store);
    pthread_mutex_unlock(&s_regLock);

    REGSTORE::const_iterator it = store.find(MakeEntryName(fullkeyname, valuename));

    if (it == store.end()) {
        SErrSetLastError(STORM_ERROR_FILE_NOT_FOUND);
        return 0;
    }

    *value = it->second;

    return 1;
}

static int32_t InternalSaveEntry(const char* keyname, const char* valuename, uint32_t flags, char type, const std::string& data) {
    char fullkeyname[MAX_PATH];
    BuildFullKeyName(keyname, flags, fullkeyname, MAX_PATH);

    REGSTORE store;
    pthread_mutex_lock(&s_regLock);
    LoadStore(store);

    REGVALUE& value = store[MakeEntryName(fullkeyname, valuename)];
    value.type = type;
    value.data = data;

    int32_t result = SaveStore(store);
    pthread_mutex_unlock(&s_regLock);

    return result;
}

int32_t STORMAPI SRegLoadString(const char* keyname, const char* valuename, uint32_t flags, char* buffer, uint32_t buffersize) {
    if (!keyname || !*keyname || !valuename || !*valuename || !buffer || !buffersize) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    REGVALUE value;

    if (!InternalLoadEntry(keyname, valuename, flags, &value)) {
        return 0;
    }

    SStrCopy(buffer, value.data.c_str(), buffersize);

    return 1;
}

int32_t STORMAPI SRegLoadValue(const char* keyname, const char* valuename, uint32_t flags, uint32_t* value) {
    if (!keyname || !*keyname || !valuename || !*valuename || !value) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    REGVALUE entry;

    if (!InternalLoadEntry(keyname, valuename, flags, &entry)) {
        return 0;
    }

    *value = static_cast<uint32_t>(strtoul(entry.data.c_str(), nullptr, 0));

    return 1;
}

int32_t STORMAPI SRegSaveString(const char* keyname, const char* valuename, uint32_t flags, const char* string) {
    if (!keyname || !*keyname || !valuename || !*valuename || !string) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    return InternalSaveEntry(keyname, valuename, flags, SREG_TYPE_STRING, string);
}

int32_t STORMAPI SRegSaveValue(const char* keyname, const char* valuename, uint32_t flags, uint32_t value) {
    if (!keyname || !*keyname || !valuename || !*valuename) {
        SErrSetLastError(STORM_ERROR_INVALID_PARAMETER);
        return 0;
    }

    return InternalSaveEntry(keyname, valuename, flags, SREG_TYPE_DWORD, std::to_string(value));
}

#endif
