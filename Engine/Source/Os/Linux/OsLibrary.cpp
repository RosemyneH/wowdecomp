#include <cstdint>
#include <dlfcn.h>
#include "Engine/Source/Os/OsLibrary.h"

void* OsLoadLibrary(const char* fileName) {
    return dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
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
