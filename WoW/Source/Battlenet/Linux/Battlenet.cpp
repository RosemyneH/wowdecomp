#if defined(WOWDECOMP_UNIX)

#include <dlfcn.h>
#include <new>
#include "WoW/Source/Battlenet/Battlenet.h"

// OFFSET: 0x009C28B0
void* Battlenet::WindowsLibrary::GetProc(const char* name) {
    return dlsym(this->m_module, name);
}

// OFFSET: 0x009C28D0
void* Battlenet::WindowsLibrary::GetProc(uint32_t ordinal) {
    return nullptr;
}

// OFFSET: 0x009C2900
Battlenet::Library* Battlenet::Library::Load(const char* path, Allocator& allocator) {
    void* module = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (!module) {
        return nullptr;
    }

    void* memory = allocator.Alloc(sizeof(WindowsLibrary));

    if (!memory) {
        dlclose(module);
        return nullptr;
    }

    WindowsLibrary* library = new (memory) WindowsLibrary();
    library->m_module = module;
    library->m_allocator = &allocator;
    return library;
}

// OFFSET: 0x009C2A30
Battlenet::WindowsLibrary::~WindowsLibrary() {
    dlclose(this->m_module);
}

#endif
