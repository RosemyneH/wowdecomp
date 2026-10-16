#ifndef ENGINE_SOURCE_STORM_SOURCE_SMSG_H
#define ENGINE_SOURCE_STORM_SOURCE_SMSG_H

#include "Engine/Source/Base/OsTypes.h"
#include "Engine/Source/Storm/H/stpl.h"

struct WNDREC : TSLinkedNode<WNDREC> {
    /* 0008 */ HWND window;
};

#endif
