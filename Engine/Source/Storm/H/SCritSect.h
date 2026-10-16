#ifndef ENGINE_SOURCE_STORM_H_SCRITSECT_H
#define ENGINE_SOURCE_STORM_H_SCRITSECT_H

#include <cstdint>
#include "Engine/Source/Base/OsTypes.h"

class CCritSect {
    public:
    /* 0000 */ CRITICAL_SECTION m_critsect;

    CCritSect();
    ~CCritSect();
    void Enter();
    void Enter(int32_t forwriting);
    void Leave();
    void Leave(int32_t fromwriting);
};

#endif
