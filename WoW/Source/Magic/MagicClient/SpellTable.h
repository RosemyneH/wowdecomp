#ifndef WOW_SOURCE_MAGIC_MAGICCLIENT_SPELLTABLE_H
#define WOW_SOURCE_MAGIC_MAGICCLIENT_SPELLTABLE_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Storm/H/stpl.h"

class SkillCostsDataRec;
class SkillLineAbilityRec;
class SkillRaceClassInfoRec;

struct HASHKEY_SKILLLINE {
    /* 0000 */ uint8_t m_race;
    /* 0001 */ uint8_t m_class;
    /* 0004 */ int32_t m_spellId;

    bool operator==(const HASHKEY_SKILLLINE& key) const {
        return this->m_race == key.m_race && this->m_class == key.m_class && this->m_spellId == key.m_spellId;
    }
};

struct RACECLASSINFO : public TSHashObject<RACECLASSINFO, HASHKEY_NONE> {
    /* 0018 */ const SkillRaceClassInfoRec* m_info;
    /* 001C */ int32_t m_count;
};

struct SKILLCOSTSINFO : public TSHashObject<SKILLCOSTSINFO, HASHKEY_NONE> {
    /* 0018 */ const SkillCostsDataRec* m_info;
    /* 001C */ int32_t m_count;
};

struct SKILLLINEREC : public TSHashObject<SKILLLINEREC, HASHKEY_SKILLLINE> {
    /* 001C */ const SkillLineAbilityRec* m_ability;
};

struct SkillLineLinkInfo {
    /* 0000 */ const SkillLineAbilityRec* m_ability;
    /* 0004 */ uint32_t m_count;
};

void SpellTableInitialize();
void SpellTableDestroy();
void SpellTableResetSkillLines();
void SpellTableSetClassSource(int (*count)(), int (*ident)(int));
bool SpellTableMatchRaceClass(int32_t race, int32_t classId, int32_t raceMask, int32_t classMask, int32_t excludeRace, int32_t excludeClass);
SkillLineAbilityRec* SpellTableLookupPetAbility(int32_t skillLine, int32_t spellId);
bool SpellTableGetSkillLineLinkInfo(int32_t skillLine, const SkillLineAbilityRec*& ability, uint32_t& count);
const SkillRaceClassInfoRec* SpellTableLookupInfo(int32_t race, int32_t classId, int32_t skillId);
SkillLineAbilityRec* SpellTableLookupAbility(int32_t race, int32_t classId, int32_t spellId);
int32_t GetSkillTierCost(const SkillRaceClassInfoRec* info, uint32_t step);

#endif
