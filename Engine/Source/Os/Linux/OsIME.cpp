#include <cstdint>
#include "Engine/Source/Os/OsIME.h"

static uint32_t s_enableCount;

OS_IME_LANGUAGEMODE OsIMEGetLanguageMode() {
    return OS_IME_MODE_ROMAN;
}

int32_t OsIMEGetCompositionString(char* buffer, uint32_t bufferSize) {
    return 0;
}

int32_t OsIMEGetCompositionResult(char* buffer, uint32_t bufferSize) {
    return 0;
}

int32_t OsIMEGetClauseInfo(uint32_t& clauseStart, uint32_t& clauseEnd, uint32_t& cursor) {
    clauseStart = 0;
    clauseEnd = 0;
    cursor = 0;
    return 0;
}

// OFFSET: 0x0086CF90
void OsIMEEnable(int32_t enable) {
    if (enable) {
        ++s_enableCount;
    } else if (s_enableCount) {
        --s_enableCount;
    }
}

void OsIMESync() {
}

void OsIMESetLanguageMode(int32_t native) {
}

void OsIMEInitialize() {
}

void OsIMEDestroy() {
    s_enableCount = 0;
}

int32_t OsIMEGetCandidates(uint32_t listMask, uint32_t& pageStart, uint32_t& pageSize, uint32_t& count, uint32_t& selection, TSGrowableArray<OsIMECandidate>& candidates) {
    pageStart = 0;
    pageSize = 0;
    count = 0;
    selection = 0;
    return 0;
}
