#include "World.h"
#include <cstdio>
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "WoW/Source/WorldClient/Types.h"
#include "WoW/Source/WorldClient/MapLoad.h"
#include "Engine/Source/Tempest/CImVector.h"

// STATIC: 0x00ADF7F0
int32_t CWorld::s_waterRipples = 1;

// VERIFY: ae98adec auto
// OFFSET: 0x0077F690
int32_t CWorld::ConsoleCommand_WaterRipples(const char* command, const char* arguments) {
    sscanf(arguments, "%d", &CWorld::s_waterRipples);
    return 1;
}

// VERIFY: 05445309 auto
// OFFSET: 0x0077F6B0
int32_t CWorld::ConsoleCommand_WaterParticulates(const char* command, const char* arguments) {
    if (CWorld::enables & Enable_Particulates) {
        ConsoleWrite("Particulates disabled", DEFAULT_COLOR);
        CWorld::enables &= ~Enable_Particulates;
    } else {
        ConsoleWrite("Particulates enabled", DEFAULT_COLOR);
        CWorld::enables |= Enable_Particulates;
    }

    return 1;
}

// VERIFY: 6b7e55e5 auto
// OFFSET: 0x0077F650
int32_t CWorld::ConsoleCommand_ShowCull(const char* command, const char* arguments) {
    if (CWorld::enables & Enable_Culling) {
        ConsoleWrite("Terrain culling disabled.", DEFAULT_COLOR);
        CWorld::enables &= ~Enable_Culling;
    } else {
        ConsoleWrite("Terrain culling enabled.", DEFAULT_COLOR);
        CWorld::enables |= Enable_Culling;
    }

    return 1;
}

// VERIFY: 70dab189 auto
// OFFSET: 0x0077F7E0
int32_t CWorld::ConsoleCommand_ShowShadow(const char* command, const char* arguments) {
    if (CWorld::enables & Enable_Shadow) {
        ConsoleWrite("Terrain shadow disabled.", DEFAULT_COLOR);
        CWorld::enables &= ~Enable_Shadow;
    } else {
        ConsoleWrite("Terrain shadow enabled.", DEFAULT_COLOR);
        CWorld::enables |= Enable_Shadow;
    }

    return 1;
}

// VERIFY: c47cd1e6 auto
// OFFSET: 0x0077F870
int32_t CWorld::ConsoleCommand_ShowSimpleDoodads(const char* command, const char* arguments) {
    if (CWorld::bShowSimpleDoodads) {
        ConsoleWrite("Simple doodads disabled.", DEFAULT_COLOR);
        CWorld::bShowSimpleDoodads = 0;
        return 1;
    }

    ConsoleWrite("Simple doodads enabled.", DEFAULT_COLOR);
    int32_t result = 1;
    CWorld::bShowSimpleDoodads = result;
    return result;
}

// VERIFY: e24c8564 auto
// OFFSET: 0x0077F5B0
int32_t CWorld::ConsoleCommand_ShowDetailDoodads(const char* command, const char* arguments) {
    if (CWorld::enables & Enable_DetailDoodads) {
        ConsoleWrite("Detail doodads disabled.", DEFAULT_COLOR);
        CWorld::enables &= ~Enable_DetailDoodads;
    } else {
        ConsoleWrite("Detail doodads enabled.", DEFAULT_COLOR);
        CWorld::enables |= Enable_DetailDoodads;
    }

    return 1;
}

// VERIFY: b924ea7d auto
// OFFSET: 0x0077F820
int32_t CWorld::ConsoleCommand_ShowLowDetail(const char* command, const char* arguments) {
    if (CWorld::enables & Enable_LowDetail) {
        ConsoleWrite("Terrain low detail disabled.", DEFAULT_COLOR);
        CWorld::enables &= ~Enable_LowDetail;
    } else {
        ConsoleWrite("Terrain low detail enabled.", DEFAULT_COLOR);
        CWorld::enables |= Enable_LowDetail;
    }

    return 1;
}

// VERIFY: cdeaae87 auto
// OFFSET: 0x0077F600
int32_t CWorld::ConsoleCommand_MaxLOD(const char* command, const char* arguments) {
    uint32_t lod;
    sscanf(arguments, "%d", &lod);

    if (lod > 3) {
        World::lodMax = 3;
        return 1;
    }

    if (lod < 2) {
        lod = 2;
    }

    World::lodMax = lod;
    return 1;
}

// VERIFY: c74f284a manual
// OFFSET: 0x0077F700
int32_t CWorld::ConsoleCommand_DetailDoodadAlpha(const char* command, const char* arguments) {
    uint32_t alphaRef;
    sscanf(arguments, "%d", &alphaRef);

    if (alphaRef <= 0xFF) {
        CWorld::detailDoodadAlphaRef = alphaRef;
    } else {
        ConsoleWrite("Alpha ref range 0 - 255.", DEFAULT_COLOR);
    }

    return 1;
}

// VERIFY: 54e3d8ec auto
// OFFSET: 0x0077F750
int32_t CWorld::ConsoleCommand_CharacterAmbient(const char* command, const char* arguments) {
    float ambient;
    sscanf(arguments, "%f", &ambient);

    if (ambient < 0.0f || ambient > 1.0f) {
        ConsoleWrite("Ambient multiply range 0.0 - 1.0.", DEFAULT_COLOR);
        return 1;
    }

    CMapStaticEntity::s_characterAmbient = ambient + ambient + 1.0f;
    s_disableAreaAmbient = ambient > 0.0f;
    return 1;
}

// VERIFY: ed3a1d1c manual
// OFFSET: 0x00780E20
int32_t CWorld::ConsoleCommand_SetShadow(const char* command, const char* arguments) {
    float color[4];
    sscanf(arguments, "%f %f %f %f", &color[0], &color[1], &color[2], &color[3]);

    for (uint32_t i = 0; i < 4; i++) {
        if (color[i] < 0.0f || color[i] > 1.0f) {
            ConsoleWrite("Color values must be in range (0.0,1.0).", DEFAULT_COLOR);
            return 0;
        }
    }

    CImVector shadow;
    shadow.b = static_cast<uint8_t>(color[3] * 255.0f);
    shadow.g = static_cast<uint8_t>(color[2] * 255.0f);
    shadow.r = static_cast<uint8_t>(color[1] * 255.0f);
    shadow.a = static_cast<uint8_t>(static_cast<int32_t>(255.0f * color[0]));
    CWorld::SetShadowColor(shadow);
    return 1;
}
