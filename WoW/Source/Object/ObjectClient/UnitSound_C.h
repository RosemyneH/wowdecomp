#ifndef WOW_SOURCE_OBJECT_OBJECTCLIENT_UNITSOUND_C_H
#define WOW_SOURCE_OBJECT_OBJECTCLIENT_UNITSOUND_C_H

#include <cstddef>
#include <cstdint>
#include "WoW/Source/Object/ObjectClient/Types.h"

class CreatureSoundDataRec;

struct DEATTHUDSOUNDINFO {
    /* 0000 */ int32_t m_soundEntryId;
    /* 0004 */ int32_t m_soundEntryIdWater;

    DEATTHUDSOUNDINFO();
};

void UnitSoundInitialize();
void UnitSoundShutdown();
int32_t GetSoundID(const CreatureSoundDataRec* soundData, UNITSOUNDTYPE soundType);
int32_t CheckUnitSoundTimer(UNITSOUNDTYPE soundType);

#endif
