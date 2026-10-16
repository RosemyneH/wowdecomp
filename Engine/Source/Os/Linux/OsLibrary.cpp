#include <dlfcn.h>
#include "Engine/Source/Os/OsLibrary.h"

void* OsLoadLibrary(const char* fileName) {
    return dlopen(fileName, RTLD_NOW | RTLD_LOCAL);
}

void OsFreeLibrary(void* module) {
    dlclose(module);
}

void* OsGetLibraryEntryPoint(void* module, const char* name) {
    return dlsym(module, name);
}
