#include <cstdint>
#include <SDL.h>
#include "Engine/Source/Os/OsClipboard.h"
#include "Engine/Source/Storm/H/storm.h"

char* IOsClipboardGetString(void* window) {
    if (!SDL_HasClipboardText()) {
        return nullptr;
    }

    char* text = SDL_GetClipboardText();
    if (!text) {
        return nullptr;
    }

    uint32_t size = static_cast<uint32_t>(SStrLen(text) + 1);
    char* string = static_cast<char*>(SMemAlloc(size, __FILE__, __LINE__, 0));
    SStrCopy(string, text, size);

    SDL_free(text);
    return string;
}

// OFFSET: 0x008727C0
void OsClipboardFreeString(char* string) {
    if (string) {
        SMemFree(string, __FILE__, __LINE__, 0);
    }
}

int32_t IOsClipboardPutString(const char* string, void* window) {
    return SDL_SetClipboardText(string) == 0;
}
