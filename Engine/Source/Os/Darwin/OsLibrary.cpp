#include <cstdint>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include "Engine/Source/Os/OsLibrary.h"

void* OsLoadLibrary(const char* fileName) {
    if (!fileName || !std::strchr(fileName, '\\')) {
        return dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
    }

    std::string path(fileName);
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }

    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

void OsFreeLibrary(void* module) {
    dlclose(module);
}

void* OsGetLibraryEntryPoint(void* module, const char* name) {
    if (reinterpret_cast<uintptr_t>(name) < 0x10000) {
        return nullptr;
    }
    return dlsym(module, name);
}
