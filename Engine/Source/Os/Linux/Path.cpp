#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include "Engine/Source/Os/Path.h"
#include "Engine/Source/Storm/H/storm.h"

static CommandLineUTF8 s_commandLineUTF8;

// OFFSET: 0x0086BBD0
uint32_t GetModuleFileNameUTF8(void* module, char* path, uint32_t size) {
    if (!size) {
        return 0;
    }

    ssize_t written = -1;

    if (module) {
        link_map* map = nullptr;

        if (!dlinfo(module, RTLD_DI_LINKMAP, &map) && map && map->l_name) {
            written = static_cast<ssize_t>(SStrCopy(path, map->l_name, size));
        }
    } else {
        written = readlink("/proc/self/exe", path, size - 1);
    }

    if (written < 0) {
        written = 0;
    }

    if (static_cast<uint32_t>(written) > size - 1) {
        written = size - 1;
    }

    path[written] = '\0';

    return static_cast<uint32_t>(written);
}

// OFFSET: 0x0086BDB0
CommandLineUTF8::CommandLineUTF8() {
    char raw[4096];
    size_t length = 0;

    FILE* file = fopen("/proc/self/cmdline", "rb");

    if (file) {
        length = fread(raw, 1, sizeof(raw) - 1, file);
        fclose(file);
    }

    while (length && raw[length - 1] == '\0') {
        length--;
    }

    for (size_t i = 0; i < length; i++) {
        if (raw[i] == '\0') {
            raw[i] = ' ';
        }
    }

    raw[length] = '\0';

    this->m_commandLine = static_cast<char*>(SMemAlloc(length + 1, __FILE__, __LINE__, 0));

    if (!this->m_commandLine) {
        return;
    }

    SStrCopy(this->m_commandLine, raw, length + 1);
}

// INLINED
CommandLineUTF8::~CommandLineUTF8() {
    SMemFree(this->m_commandLine, __FILE__, __LINE__, 0);
}
