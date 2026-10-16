#include <climits>
#include <cstdint>
#include <cstdlib>
#include <crt_externs.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include "Engine/Source/Os/Path.h"
#include "Engine/Source/Storm/H/storm.h"

static CommandLineUTF8 s_commandLineUTF8;

static const char* FindModuleImageName(void* module) {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char* name = _dyld_get_image_name(i);
        void* handle = dlopen(name, RTLD_NOLOAD | RTLD_LAZY);

        if (!handle) {
            continue;
        }

        dlclose(handle);

        if (handle == module) {
            return name;
        }
    }

    return nullptr;
}

// OFFSET: 0x0086BBD0
uint32_t GetModuleFileNameUTF8(void* module, char* path, uint32_t size) {
    if (!size) {
        return 0;
    }

    path[0] = '\0';

    if (module) {
        const char* name = FindModuleImageName(module);

        if (name) {
            SStrCopy(path, name, size);
        }
    } else {
        char executable[PATH_MAX];
        char resolved[PATH_MAX];
        uint32_t length = sizeof(executable);

        if (!_NSGetExecutablePath(executable, &length)) {
            SStrCopy(path, realpath(executable, resolved) ? resolved : executable, size);
        }
    }

    return static_cast<uint32_t>(SStrLen(path));
}

// OFFSET: 0x0086BDB0
CommandLineUTF8::CommandLineUTF8() {
    int argc = *_NSGetArgc();
    char** argv = *_NSGetArgv();
    size_t length = 0;

    for (int i = 0; i < argc; i++) {
        length += SStrLen(argv[i]) + 1;
    }

    this->m_commandLine = static_cast<char*>(SMemAlloc(length + 1, __FILE__, __LINE__, 0));

    if (!this->m_commandLine) {
        return;
    }

    this->m_commandLine[0] = '\0';

    for (int i = 0; i < argc; i++) {
        if (i) {
            SStrPack(this->m_commandLine, " ", length + 1);
        }

        SStrPack(this->m_commandLine, argv[i], length + 1);
    }
}

// INLINED
CommandLineUTF8::~CommandLineUTF8() {
    SMemFree(this->m_commandLine, __FILE__, __LINE__, 0);
}
