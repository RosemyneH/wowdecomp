#include "MapHorizonTrees.h"
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Model2/M2Cache.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/Types.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/MapLoad.h"
#include "WoW/Source/WorldClient/MapLowDetail.h"
#include "WoW/Source/WorldClient/Types.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/WorldParam_CVarSlider.h"

struct HorizonTreeInst {
    char m_model[260];
    C44Matrix m_world;
};

struct HorizonTileTrees {
    std::vector<HorizonTreeInst> m_trees;
    int32_t m_parsed;
};

static std::unordered_map<uint32_t, HorizonTileTrees> s_tileTrees;
static std::unordered_map<std::string, CM2Model*> s_prototypes;

static uint32_t TileKey(uint32_t x, uint32_t y) {
    return x | (y << 6);
}

static int32_t IsTreeModelPath(const char* path) {
    if (!path || !path[0]) {
        return 0;
    }

    char lower[260];
    SStrCopy(lower, path, sizeof(lower));
    SStrLower(lower);

    if (!SStrStr(lower, "tree")) {
        return 0;
    }

    if (SStrStr(lower, "treasure") || SStrStr(lower, "street")) {
        return 0;
    }

    return 1;
}

static C44Matrix DoodadWorldMatrix(const SMDoodadDef* doodad, const C3Vector& center) {
    C44Matrix mat;
    C3Vector position;
    position.x = -doodad->m_position.z + center.x;
    position.y = -doodad->m_position.x + center.y;
    position.z = doodad->m_position.y + center.z;

    float rotX = doodad->m_rotation.z * 0.017453292f;
    float rotY = doodad->m_rotation.x * 0.017453292f;
    float rotZ = doodad->m_rotation.y * 0.017453292f;
    float scale = doodad->m_scale * 0.0009765625f;

    mat.d0 = position.x;
    mat.d1 = position.y;
    mat.d2 = position.z;
    mat.RotateAroundZ(rotZ + 3.1415927f);
    mat.RotateAroundY(rotY);
    mat.RotateAroundX(rotX);
    mat.Scale(scale);
    return mat;
}

static int32_t ParseAdtTile(uint32_t tileX, uint32_t tileY, HorizonTileTrees& out) {
    char fileName[256];
    SStrPrintf(fileName, sizeof(fileName), "%s\\%s_%u_%u.adt", CMap::mapPath, CMap::mapName, tileX, tileY);

    SFileImpl* file = nullptr;

    if (!CMap::SafeOpen(fileName, &file) || !file) {
        out.m_parsed = 1;
        return 0;
    }

    uint32_t size = SFileGetFileSize(file, nullptr);

    if (!size) {
        SFileCloseFile(file);
        out.m_parsed = 1;
        return 0;
    }

    void* buffer = SMemAlloc(size, __FILE__, __LINE__, 0);

    if (!buffer) {
        SFileCloseFile(file);
        out.m_parsed = 1;
        return 0;
    }

    SFileReadFile(file, buffer, size, nullptr, nullptr, 0);
    SFileCloseFile(file);

    SIffChunk* version = static_cast<SIffChunk*>(buffer);
    uint8_t* header = reinterpret_cast<uint8_t*>(&version->m_data) + version->m_size + offsetof(SIffChunk, m_data);
    SMAreaHeader* areaHeader = reinterpret_cast<SMAreaHeader*>(header);

    SMDoodadDef* doodads = reinterpret_cast<SMDoodadDef*>(&reinterpret_cast<SIffChunk*>(header + areaHeader->m_mddf)->m_data);
    uint32_t doodadCount = reinterpret_cast<SIffChunk*>(header + areaHeader->m_mddf)->m_size / sizeof(SMDoodadDef);
    char* m2Names = reinterpret_cast<char*>(&reinterpret_cast<SIffChunk*>(header + areaHeader->m_mmdx)->m_data);
    uint32_t* m2Offsets = reinterpret_cast<uint32_t*>(&reinterpret_cast<SIffChunk*>(header + areaHeader->m_mmid)->m_data);

    C3Vector center(17066.666f, 17066.666f, 0.0f);
    out.m_trees.clear();

    for (uint32_t i = 0; i < doodadCount; ++i) {
        const char* name = &m2Names[m2Offsets[doodads[i].m_nameId]];

        if (!IsTreeModelPath(name)) {
            continue;
        }

        HorizonTreeInst inst = {};
        SStrCopy(inst.m_model, name, sizeof(inst.m_model));
        inst.m_world = DoodadWorldMatrix(&doodads[i], center);
        out.m_trees.push_back(inst);
    }

    SMemFree(buffer, nullptr, 0, 0);
    out.m_parsed = 1;
    return 1;
}

static CM2Model* TreePrototype(const char* modelPath) {
    auto found = s_prototypes.find(modelPath);

    if (found != s_prototypes.end()) {
        return found->second;
    }

    if (!World::s_m2Scene) {
        return nullptr;
    }

    CM2Model* model = World::s_m2Scene->CreateModel(modelPath, 0x20);

    if (!model) {
        return nullptr;
    }

    model->WaitForLoad(nullptr);
    model->SetAnimating(0);
    model->m_lightingCallback = CMapStaticEntity::ModelLightingCallback;
    s_prototypes.emplace(modelPath, model);
    return model;
}

static void LinkDraw(CM2Model* model) {
    model->m_fFlags |= 0x8u;
    model->m_fFlags |= 0x10000u;

    if (model->m_drawPrev) {
        return;
    }

    CM2Model** head = &model->m_scene->m_drawList;
    model->m_drawPrev = head;
    model->m_drawNext = *head;
    *head = model;

    if (model->m_drawNext) {
        model->m_drawNext->m_drawPrev = &model->m_drawNext;
    }
}

static void UnlinkDraw(CM2Model* model) {
    if (!model->m_drawPrev) {
        return;
    }

    *model->m_drawPrev = model->m_drawNext;

    if (model->m_drawNext) {
        model->m_drawNext->m_drawPrev = model->m_drawPrev;
    }

    model->m_drawPrev = nullptr;
    model->m_drawNext = nullptr;
    model->m_fFlags &= ~0x10008u;
}

struct M2PassSnapshot {
    uint32_t elements;
    uint32_t doodads;
    uint32_t pass[3];
};

static M2PassSnapshot SnapshotScene(CM2Scene* scene) {
    M2PassSnapshot snap = {};
    snap.elements = scene->m_elements.m_count;
    snap.doodads = scene->m_doodadElements.m_count;

    for (uint32_t i = 0; i < 3; ++i) {
        snap.pass[i] = scene->m_passElements[i].m_count;
    }

    return snap;
}

static void RestoreScene(CM2Scene* scene, const M2PassSnapshot& snap) {
    scene->m_elements.m_count = snap.elements;
    scene->m_doodadElements.m_count = snap.doodads;

    for (uint32_t i = 0; i < 3; ++i) {
        scene->m_passElements[i].m_count = snap.pass[i];
    }
}

void CMapHorizonTrees::Clear() {
    for (auto& entry : s_prototypes) {
        if (entry.second) {
            entry.second->SetAnimating(0);
            UnlinkDraw(entry.second);
        }
    }

    s_prototypes.clear();
    s_tileTrees.clear();
}

void CMapHorizonTrees::Draw() {
    if (!CWorldParamSlider::ExtendedViewEnabled() || !World::s_m2Scene || !(CWorld::enables & Enable_Doodads)) {
        return;
    }

    constexpr uint32_t kTileParseBudget = 2;
    constexpr uint32_t kDrawBudget = 96;
    uint32_t parsed = 0;
    uint32_t drawn = 0;

    std::vector<HorizonTreeInst> drawList;
    drawList.reserve(kDrawBudget);

    for (uint32_t i = 0; i < 4096 && (parsed < kTileParseBudget || drawList.size() < kDrawBudget); ++i) {
        CMapAreaLow* area = CMap::s_lowDetail.m_areas[i];

        if (!area || !(area->m_flags & 2u)) {
            continue;
        }

        uint32_t key = TileKey(area->m_index.x, area->m_index.y);
        HorizonTileTrees& tile = s_tileTrees[key];

        if (!tile.m_parsed && parsed < kTileParseBudget) {
            ParseAdtTile(area->m_index.x, area->m_index.y, tile);
            parsed++;
        }

        for (const HorizonTreeInst& inst : tile.m_trees) {
            if (drawList.size() >= kDrawBudget) {
                break;
            }

            drawList.push_back(inst);
        }
    }

    if (drawList.empty()) {
        return;
    }

    CM2Scene* scene = World::s_m2Scene;
    M2PassSnapshot snap = SnapshotScene(scene);
    std::vector<CM2Model*> temps;
    temps.reserve(drawList.size());

    GxRsSet(GxRs_Lighting, 0);

    for (const HorizonTreeInst& inst : drawList) {
        CM2Model* proto = TreePrototype(inst.m_model);

        if (!proto || !proto->IsLoaded(0, 0)) {
            continue;
        }

        CM2Model* model = scene->DuplicateModel(proto, 0x20);

        if (!model) {
            continue;
        }

        model->m_worldTransform = inst.m_world;
        model->m_lightingCallback = CMapStaticEntity::ModelLightingCallback;
        model->SetAnimating(1);
        model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        LinkDraw(model);
        temps.push_back(model);
        drawn++;
    }

    if (!drawn) {
        RestoreScene(scene, snap);
        return;
    }

    scene->Animate(CWorldScene::s_activeWorldView);
    scene->Draw(M2PASS_0);
    RestoreScene(scene, snap);

    for (CM2Model* model : temps) {
        model->SetAnimating(0);
        UnlinkDraw(model);

        if (g_modelPool) {
            g_modelPool->Delete(model);
        }
    }
}
