#include "MapLowDetail.h"
#include <algorithm>
#include "WoW/Source/WorldClient/WorldConstants.h"
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CMath.h"
#include "WoW/Source/WorldClient/CWorldOcclusion.h"
#include "WoW/Source/WorldClient/FVBBList.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/MapLight.h"
#include "WoW/Source/WorldClient/SortTable.h"
#include "WoW/Source/WorldClient/MapObj.h"
#include "WoW/Source/WorldClient/MapObjDef.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/WorldParam_CVarSlider.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/CGxVertexPC.h"
#include "Engine/Source/Gx/CGxVertexPCT.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/Types.h"

// VERIFY: 3763adce auto
// OFFSET: 0x007C06E0
CMapAreaLow::CMapAreaLow() {
    this->m_sphere.r = 0.0f;
    this->m_index.x = 0;
    this->m_index.y = 0;
    this->m_horizonLink.m_prevLink = nullptr;
    this->m_horizonLink.m_next = nullptr;
    this->m_indexCount = 0;
    this->m_block = nullptr;
    this->m_flags = 0;
    this->m_heights = nullptr;
    this->m_holes = nullptr;
}

// VERIFY: 0b659a65 auto
// OFFSET: 0x007CC2C0
CMapLowDetail::CMapLowDetail() {
    this->m_data = nullptr;
    this->m_mapObjNames = nullptr;
    this->m_mapObjNameOffsets = nullptr;
    this->m_mapObjDefs = nullptr;
    this->m_mapObjDefCount = 0;
    this->m_areaOffsets = nullptr;
    memset(this->m_areas, 0, sizeof(this->m_areas));
}

// OFFSET: 0x007CBFE0
void CMapLowDetail::Purge() {
    for (uint32_t i = 0; i < 4096; i++) {
        CMapAreaLow* areaLow = this->m_areas[i];

        if (areaLow) {
            if (areaLow->m_block) {
                CMap::s_vbbList.Release(areaLow->m_block);
            }

            this->m_areas[i]->m_block = nullptr;
        }
    }
}

// VERIFY: 3b80ddc7 manual
// OFFSET: 0x007CC0B0
void CMapLowDetail::CullHorizonAreas(const C3Vector* pos) {
    float y = -(pos->y - 17066.666f);
    float x = -(pos->x - 17066.666f) * OO_COORD_TO_CHUNK;
    int32_t row = CMath::fint_(x - 0.5f);
    y = y * OO_COORD_TO_CHUNK;
    int32_t col = CMath::fint_(y - 0.5f);
    int32_t radius = static_cast<int32_t>(static_cast<int64_t>(World::s_horizonFarClipScale * World::s_farClip * 0.030000001f));

    // ʕ •ᴥ•ʔ✿ The WDL is the whole 64 tile continent. Frustum tests drop the tiles behind the camera ✿ ʕ •ᴥ•ʔ
    if (radius < 1024) {
        radius = 1024;
    }

    int32_t minCol = ((col - radius) >> 4) - 2;
    int32_t minRow = ((row - radius) >> 4) - 2;
    int32_t maxCol = ((col + radius) >> 4) + 2;
    int32_t maxRow = ((radius + row) >> 4) + 2;

    if (minRow < 0) {
        minRow = 0;
    }

    if (minCol < 0) {
        minCol = 0;
    }

    if (maxCol >= 64) {
        maxCol = 63;
    }

    if (maxRow >= 64) {
        maxRow = 63;
    }

    float horizonNear;
    float horizonFar;
    World::HorizonClip(horizonNear, horizonFar);
    (void)horizonFar;
    const int32_t extended = CWorldParamSlider::ExtendedViewEnabled();

    for (int32_t r = minRow; r <= maxRow; r++) {
        for (int32_t c = minCol; c <= maxCol; c++) {
            CMapAreaLow* area = this->m_areas[64 * r + c];

            if (!area) {
                continue;
            }

            if (!extended && CWorldScene::FrustumCull(&area->m_bbox)) {
                continue;
            }

            if (extended) {
                float vx = area->m_sphere.c.x - pos->x;
                float vy = area->m_sphere.c.y - pos->y;
                float vz = area->m_sphere.c.z - pos->z;
                float forward = vx * CWorldScene::camVec.x + vy * CWorldScene::camVec.y + vz * CWorldScene::camVec.z;

                if (forward < -area->m_sphere.r) {
                    continue;
                }
            }

            float x = pos->x;

            if (x < area->m_bbox.b.x) {
                x = area->m_bbox.b.x;
            } else if (x > area->m_bbox.t.x) {
                x = area->m_bbox.t.x;
            }

            float y = pos->y;

            if (y < area->m_bbox.b.y) {
                y = area->m_bbox.b.y;
            } else if (y > area->m_bbox.t.y) {
                y = area->m_bbox.t.y;
            }

            float dx = pos->x - x;
            float dy = pos->y - y;

            // ʕ •ᴥ•ʔ✿ Overlap the resident ring by one tile. Drawing the whole continent every frame was the draw flood ✿ ʕ •ᴥ•ʔ
            float limit = extended ? World::DetailSpanYards() - 400.0f : horizonNear;

            if (limit < 64.0f) {
                limit = 64.0f;
            }

            if (dx * dx + dy * dy < limit * limit) {
                continue;
            }

            if (!extended) {
                if (CWorldOcclusion::QueryVolumes(&area->m_sphere)) {
                    continue;
                }

                if (World::s_farClip > 300.0f && CWorldOcclusion::QueryBuffer(&area->m_bbox, 40)) {
                    continue;
                }
            }

            CWorldScene::AddHorizonChunk(area);
        }
    }

    for (uint32_t i = 0; i < this->m_mapObjDefList.Count(); i++) {
        CMapObjDef* def = this->m_mapObjDefList.m_data[i];

        if (def->m_owner->m_isGroupLoaded && !CWorldScene::FrustumCull(&def->m_bbox) && !CWorldOcclusion::QueryVolumes(&def->m_sphere)) {
            CWorldScene::AddHorizonMapObjDef(def);
        }
    }
}

// VERIFY: ba76c774 manual
// OFFSET: 0x007CC310
void CMapLowDetail::Load(const char* mapPath, const char* mapName) {
    char fileName[256];
    SFileImpl* file;

    SStrPrintf(fileName, sizeof(fileName), "%s\\%s.wdl", mapPath, mapName);
    SFileOpenFile(fileName, &file);

    if (!file) {
        return;
    }

    uint32_t size = SFileGetFileSize(file, nullptr);
    this->m_data = static_cast<char*>(SMemAlloc(size, __FILE__, __LINE__, 0));
    SFileReadFile(file, this->m_data, size, nullptr, nullptr, 0);
    SFileCloseFile(file);

    uint32_t* chunk = reinterpret_cast<uint32_t*>(this->m_data + reinterpret_cast<uint32_t*>(this->m_data)[1] + 8);
    char* data = this->m_data + reinterpret_cast<uint32_t*>(this->m_data)[1] + 16;

    if (chunk[0] == 0x4D574D4F) {
        this->m_mapObjNames = data;
        uint32_t* mwid = reinterpret_cast<uint32_t*>(data + chunk[1]);
        this->m_mapObjNameOffsets = mwid + 2;
        uint32_t* modf = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(mwid) + mwid[1] + 8);
        this->m_mapObjDefs = reinterpret_cast<SMMapObjDef*>(modf + 2);
        this->m_mapObjDefCount = modf[1] >> 6;
        data = reinterpret_cast<char*>(modf + 2) + modf[1] + 8;
    }

    this->m_areaOffsets = reinterpret_cast<uint32_t*>(data);

    uint32_t index = 0;

    for (uint32_t y = 0; y < 1024; y += 16) {
        for (uint32_t x = 0; x < 1024; x += 16) {
            if (this->m_areaOffsets[index]) {
                char* mare = this->m_data + this->m_areaOffsets[index];
                CMapAreaLow* area = CMap::AllocAreaLow();
                this->m_areas[index] = area;
                area->m_heights = reinterpret_cast<int16_t*>(mare + 8);
                uint32_t* next = reinterpret_cast<uint32_t*>(mare + reinterpret_cast<uint32_t*>(mare)[1] + 8);

                int32_t minHeight = 100000;
                int32_t maxHeight = -100000;

                for (uint32_t i = 0; i < 545; i++) {
                    int32_t height = area->m_heights[i];

                    if (height > maxHeight) {
                        maxHeight = height;
                    }

                    if (height < minHeight) {
                        minHeight = height;
                    }
                }

                area->m_index.x = x >> 4;
                area->m_index.y = y >> 4;

                float top = 17066.666f - static_cast<float>(y) * 33.333332f;
                area->m_topLeft.x = top;
                area->m_bbox.t.x = top;

                float left = static_cast<float>(x) * -33.333332f + 17066.666f;
                area->m_topLeft.y = left;
                area->m_bbox.t.y = left;
                area->m_bbox.t.z = static_cast<float>(maxHeight);
                area->m_bbox.b.x = top - 533.33331f;
                area->m_bbox.b.y = left - 533.33331f;
                area->m_bbox.b.z = static_cast<float>(minHeight);

                area->m_sphere.c.x = (area->m_bbox.t.x + area->m_bbox.b.x) * 0.5f;
                area->m_sphere.c.y = (area->m_bbox.b.y + area->m_bbox.t.y) * 0.5f;
                area->m_sphere.c.z = 0.5f * (area->m_bbox.b.z + area->m_bbox.t.z);
                float dz = area->m_bbox.t.z - area->m_sphere.c.z;
                float dy = area->m_bbox.t.y - area->m_sphere.c.y;
                float dx = area->m_bbox.t.x - area->m_sphere.c.x;
                area->m_sphere.r = sqrtf(dx * dx + dy * dy + dz * dz);

                if (next[0] == 0x4D41484F) {
                    uint16_t* holes = reinterpret_cast<uint16_t*>(next + 2);
                    area->m_holes = holes;
                    area->m_indexCount = 0;

                    for (uint32_t row = 0; row < 16; row++) {
                        for (uint32_t bit = 0; bit < 16; bit++) {
                            if (!((1 << bit) & holes[row])) {
                                area->m_indexCount += 12;
                            }
                        }
                    }
                } else {
                    area->m_holes = nullptr;
                    area->m_indexCount = 3072;
                }
            }

            index++;
        }
    }

    C3Vector center(17066.666f, 17066.666f, 0.0f);
    this->m_mapObjDefList.SetCount(this->m_mapObjDefCount);

    for (uint32_t i = 0; i < this->m_mapObjDefCount; i++) {
        SMMapObjDef* def = &this->m_mapObjDefs[i];
        CMapObjDef* mapObjDef = CMap::CreateMapObjDef(this->m_mapObjNames + this->m_mapObjNameOffsets[def->m_nameId], def, &center, false);

        if (!CMap::s_isStreamingMode) {
            CMapObj* mapObj = mapObjDef->m_owner;

            if (!mapObj->m_isGroupLoaded) {
                mapObj->WaitLoad();
            }

            uint32_t groupCount = mapObj->m_groupInfoCount;

            for (uint32_t group = 0; group < groupCount; group++) {
                if (!mapObj->IsGroupLoaded(group)) {
                    mapObj->ReadGroup(group);
                    mapObj->WaitLoadGroup(group);
                }
            }
        }

        mapObjDef->m_refCount++;
        this->m_mapObjDefList.m_data[i] = mapObjDef;
    }
}

static C3Vector LodNormal(const int16_t* heights, int32_t row, int32_t col) {
    auto height = [&](int32_t r, int32_t c) {
        return static_cast<float>(heights[r * 17 + c]);
    };

    float dzRow = row == 0 ? height(1, col) - height(0, col) : row == 16 ? height(16, col) - height(15, col) : (height(row + 1, col) - height(row - 1, col)) * 0.5f;
    float dzCol = col == 0 ? height(row, 1) - height(row, 0) : col == 16 ? height(row, 16) - height(row, 15) : (height(row, col + 1) - height(row, col - 1)) * 0.5f;
    C3Vector normal(dzRow, dzCol, 33.333332f);
    normal.NormalizeSafe();
    return normal;
}

// ʕ •ᴥ•ʔ✿ Mod2x treats 128 as the map photo. The sun lifts faces toward it and the shade keeps the zone ambient ✿ ʕ •ᴥ•ʔ
static void ShadeLodTile(CGxBuf* verts, const int16_t* heights, const C3Vector& toward, const C3Vector& ambient, const C3Vector& diffuse) {
    CGxVertexPCT* vertex = reinterpret_cast<CGxVertexPCT*>(g_theGxDevicePtr->BufLock(verts));
    float day = (ambient.x + ambient.y + ambient.z + diffuse.x + diffuse.y + diffuse.z) * (1.0f / 3.0f);

    if (day < 0.15f) {
        day = 0.15f;
    }

    if (day > 1.15f) {
        day = 1.15f;
    }

    for (int32_t row = 0; row < 17; ++row) {
        for (int32_t col = 0; col < 17; ++col) {
            C3Vector normal = LodNormal(heights, row, col);
            float ndotl = normal.x * toward.x + normal.y * toward.y + normal.z * toward.z;

            if (ndotl < 0.0f) {
                ndotl = 0.0f;
            }

            float mul = (1.08f + 0.7f * (ndotl - 0.42f)) * day;

            if (mul < 0.12f) {
                mul = 0.12f;
            }

            C3Vector tone = ambient + diffuse * ndotl;
            float luma = (tone.x + tone.y + tone.z) * (1.0f / 3.0f);
            C3Vector rgb = luma > 0.02f ? tone * (mul / luma) : C3Vector(mul, mul, mul);

            auto channel = [](float value) {
                if (value < 0.0f) {
                    value = 0.0f;
                }

                if (value > 1.0f) {
                    value = 1.0f;
                }

                return static_cast<uint8_t>(value * 127.5f + 0.5f);
            };

            vertex->c = CImVector(channel(rgb.z), channel(rgb.y), channel(rgb.x), 0xFF);
            vertex++;
        }
    }

    g_theGxDevicePtr->BufUnlock(verts, 0);
}

static std::unordered_map<std::string, std::string>& MinimapTable() {
    static std::unordered_map<std::string, std::string> table;
    static int32_t loaded = 0;

    if (loaded) {
        return table;
    }

    loaded = 1;
    SFileImpl* file = nullptr;
    SFileOpenFile("textures\\Minimap\\md5translate.trs", &file);

    if (!file) {
        return table;
    }

    uint32_t size = SFileGetFileSize(file, nullptr);
    char* data = static_cast<char*>(SMemAlloc(size + 1, __FILE__, __LINE__, 0));
    SFileReadFile(file, data, size, nullptr, nullptr, 0);
    SFileCloseFile(file);
    data[size] = 0;

    char* line = data;

    while (line && *line) {
        char* next = std::strchr(line, '\n');

        if (next) {
            *next = 0;
            next++;
        }

        if (std::strncmp(line, "dir:", 4) == 0 || !line[0]) {
            line = next;
            continue;
        }

        if (line[std::strlen(line) - 1] == '\r') {
            line[std::strlen(line) - 1] = 0;
        }

        char* tab = std::strchr(line, '\t');

        if (!tab) {
            tab = std::strchr(line, ' ');
        }

        if (tab && tab[1]) {
            *tab = 0;
            table.emplace(line, tab + 1);
        }

        line = next;
    }

    SMemFree(data, nullptr, 0, 0);
    return table;
}

static CTexture* LoadMinimap(uint32_t tileX, uint32_t tileY) {
    char key[128];
    SStrPrintf(key, sizeof(key), "%s\\map%02u_%02u.blp", CMap::mapName, tileX, tileY);
    const auto& table = MinimapTable();
    auto found = table.find(key);

    if (found == table.end()) {
        return nullptr;
    }

    char path[260];
    SStrPrintf(path, sizeof(path), "textures\\Minimap\\%s", found->second.c_str());
    // ʕ •ᴥ•ʔ✿ These BLPs have no mip chain. A mip filter samples the empty levels and turns the map into a grid ✿ ʕ •ᴥ•ʔ
    CStatus status;
    CGxTexFlags texFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
    CTexture* texture = TextureCreate(path, texFlags, &status, 1);

    if (!texture || !SStrCmp(texture->m_filename, "SolidTexture", 32)) {
        return nullptr;
    }

    return texture;
}

// ʕ •ᴥ•ʔ✿ One mesh and one minimap texture per ADT. Buildings stay on the horizon object pass ✿ ʕ •ᴥ•ʔ
void CMap::DrawHorizonList() {
    constexpr int32_t kGrid = 17;
    constexpr uint32_t kVerts = 289;

    struct Tile {
        CMapAreaLow* area;
        CGxBuf* verts;
        CGxBuf* indices;
        uint32_t indexCount;
        uint32_t baseVertex;
        uint32_t firstIndex;
        CTexture* texture;
        int32_t tried;
        int32_t lightStamp;
    };

    static std::vector<Tile> tiles;
    static CGxPool* vertPool = nullptr;
    static CGxPool* indexPool = nullptr;
    static char* builtFrom = nullptr;

    for (uint32_t i = 0; i < 4096; ++i) {
        CMapAreaLow* area = CMap::s_lowDetail.m_areas[i];

        if (area) {
            area->m_flags &= ~2u;
        }
    }

    CMapAreaLow* next = nullptr;

    for (CMapAreaLow* area = CWorldScene::sortTable.m_horizonChunkList.Head(); area; area = next) {
        next = CWorldScene::sortTable.m_horizonChunkList.Next(area);
        area->m_flags |= 2u;
        area->m_horizonLink.Unlink();
    }

    if (builtFrom != CMap::s_lowDetail.m_data) {
        for (Tile& tile : tiles) {
            if (tile.verts) {
                g_theGxDevicePtr->BufDestroy(tile.verts);
            }

            if (tile.indices) {
                g_theGxDevicePtr->BufDestroy(tile.indices);
            }
        }

        if (vertPool) {
            g_theGxDevicePtr->PoolDestroy(vertPool);
            vertPool = nullptr;
        }

        if (indexPool) {
            g_theGxDevicePtr->PoolDestroy(indexPool);
            indexPool = nullptr;
        }

        tiles.clear();

        uint32_t tileCount = 0;
        uint32_t indexTotal = 0;

        for (uint32_t i = 0; i < 4096; ++i) {
            CMapAreaLow* area = CMap::s_lowDetail.m_areas[i];

            if (!area || static_cast<int32_t>(area->m_indexCount) <= 0 || !area->m_heights) {
                continue;
            }

            uint32_t indexCount = 0;
            const uint16_t* holes = area->m_holes;

            for (int32_t row = 0; row < 16; ++row) {
                for (int32_t col = 0; col < 16; ++col) {
                    if (holes && (holes[row] & (1 << col)) != 0) {
                        continue;
                    }

                    indexCount += 6;
                }
            }

            if (!indexCount) {
                continue;
            }

            tileCount++;
            indexTotal += indexCount;
        }

        if (tileCount) {
            vertPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Static, static_cast<int32_t>(tileCount * kVerts * sizeof(CGxVertexPCT)), static_cast<EGxPoolHintBits>(0), "LodVerts");
            indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Static, static_cast<int32_t>(indexTotal * sizeof(uint16_t)), static_cast<EGxPoolHintBits>(0), "LodIdx");
        }

        uint32_t vertAt = 0;
        uint32_t indexAt = 0;

        for (uint32_t i = 0; i < 4096; ++i) {
            CMapAreaLow* area = CMap::s_lowDetail.m_areas[i];

            if (!area || static_cast<int32_t>(area->m_indexCount) <= 0 || !area->m_heights) {
                continue;
            }

            uint16_t scratch[16 * 16 * 6];
            uint32_t indexCount = 0;
            const uint16_t* holes = area->m_holes;

            for (int32_t row = 0; row < 16; ++row) {
                for (int32_t col = 0; col < 16; ++col) {
                    int32_t blocked = holes && (holes[row] & (1 << col)) != 0;

                    if (blocked) {
                        continue;
                    }

                    uint16_t a = static_cast<uint16_t>(row * kGrid + col);
                    uint16_t b = static_cast<uint16_t>(a + 1);
                    uint16_t c = static_cast<uint16_t>(a + kGrid);
                    uint16_t d = static_cast<uint16_t>(c + 1);
                    scratch[indexCount++] = a;
                    scratch[indexCount++] = b;
                    scratch[indexCount++] = c;
                    scratch[indexCount++] = b;
                    scratch[indexCount++] = d;
                    scratch[indexCount++] = c;
                }
            }

            if (!indexCount) {
                continue;
            }

            Tile tile = {};
            tile.area = area;
            tile.indexCount = indexCount;
            tile.baseVertex = vertAt / sizeof(CGxVertexPCT);
            tile.firstIndex = indexAt / sizeof(uint16_t);
            tile.verts = g_theGxDevicePtr->BufCreate(vertPool, sizeof(CGxVertexPCT), kVerts, vertAt);
            tile.indices = g_theGxDevicePtr->BufCreate(indexPool, sizeof(uint16_t), indexCount, indexAt);
            vertAt += kVerts * sizeof(CGxVertexPCT);
            indexAt += indexCount * sizeof(uint16_t);

            CGxVertexPCT* vertex = reinterpret_cast<CGxVertexPCT*>(g_theGxDevicePtr->BufLock(tile.verts));
            uint16_t* indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(tile.indices));

            for (int32_t row = 0; row < kGrid; ++row) {
                for (int32_t col = 0; col < kGrid; ++col) {
                    vertex->p.x = area->m_topLeft.x - static_cast<float>(row) * 33.333332f;
                    vertex->p.y = area->m_topLeft.y - static_cast<float>(col) * 33.333332f;
                    vertex->p.z = area->m_heights[row * 17 + col];
                    vertex->c = CImVector(0xFF, 0xFF, 0xFF, 0xFF);
                    vertex->tc.x = static_cast<float>(col) / 16.0f;
                    vertex->tc.y = static_cast<float>(row) / 16.0f;
                    vertex++;
                }
            }

            memcpy(indices, scratch, indexCount * sizeof(uint16_t));
            g_theGxDevicePtr->BufUnlock(tile.verts, 0);
            g_theGxDevicePtr->BufUnlock(tile.indices, 0);
            tile.verts->m_hasData = 1;
            tile.indices->m_hasData = 1;
            tiles.push_back(tile);
        }

        builtFrom = CMap::s_lowDetail.m_data;
    }

    int32_t loads = 0;
    const int32_t budget = CWorldParamSlider::ExtendedViewEnabled() ? 8 : 4;

    for (Tile& tile : tiles) {
        if (!(tile.area->m_flags & 2u) || tile.tried || loads >= budget) {
            continue;
        }

        tile.texture = LoadMinimap(tile.area->m_index.x, tile.area->m_index.y);
        tile.tried = 1;
        loads++;
    }

    C3Vector toward(0.0f, 0.0f, 1.0f);
    C3Vector ambient(0.45f, 0.48f, 0.52f);
    C3Vector diffuse(0.55f, 0.52f, 0.48f);

    if (CMap::s_mapLight) {
        toward = -CMap::s_mapLight->m_light.m_dir;

        if (toward.SquaredMag() > 0.00001f) {
            toward.NormalizeSafe();
        } else {
            toward = C3Vector(0.0f, 0.0f, 1.0f);
        }

        ambient = CMap::s_mapLight->m_light.m_ambColor;
        diffuse = CMap::s_mapLight->m_light.m_dirColor;
    }

    auto quantize = [](float value) {
        return static_cast<int32_t>(value * 64.0f);
    };

    int32_t key[9] = {
        quantize(toward.x), quantize(toward.y), quantize(toward.z),
        quantize(ambient.x), quantize(ambient.y), quantize(ambient.z),
        quantize(diffuse.x), quantize(diffuse.y), quantize(diffuse.z)
    };
    static int32_t lightKey[9] = {};
    static int32_t lightStamp = 1;

    if (std::memcmp(key, lightKey, sizeof(key)) != 0) {
        std::memcpy(lightKey, key, sizeof(key));
        lightStamp++;
    }

    g_theGxDevicePtr->m_xforms[GxXform_Tex0].Identity();
    g_theGxDevicePtr->m_texGen[0].Identity();
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_TexGen0, GxTexGen_Disabled);
    GxRsSet(GxRs_TextureShader0, 0);
    GxRsSet(GxRs_TextureCoord0, 0);
    GxRsSet(GxRs_AlphaOp0, GxTexOp_Mod);
    GxRsSet(GxRs_Texture1, static_cast<void*>(nullptr));

    static std::vector<GxTerrainDraw> batch;
    batch.clear();
    C44Matrix world;
    CGxMatrixStack& worldStack = g_theGxDevicePtr->m_xforms[GxXform_World];
    world = worldStack.m_mtx[worldStack.m_level];
    C44Matrix ident;
    ident.Identity();
    CGxBuf* bindVerts = nullptr;
    CGxBuf* bindIndices = nullptr;
    CGxTex* bindTex = nullptr;
    int32_t bindless = g_theGxDevicePtr->TerrainBindless();

    for (Tile& tile : tiles) {
        if (!(tile.area->m_flags & 2u)) {
            continue;
        }

        CGxTex* gx = nullptr;

        if (tile.texture && SStrCmp(tile.texture->m_filename, "SolidTexture", 32) != 0) {
            gx = TextureGetGxTex(tile.texture, 0, nullptr);
        }

        if (gx && tile.lightStamp != lightStamp) {
            ShadeLodTile(tile.verts, tile.area->m_heights, toward, ambient, diffuse);
            tile.lightStamp = lightStamp;
        }

        if (gx && bindless) {
            GxTexSetWrap(gx, GxTex_Clamp, GxTex_Clamp);
            uint32_t slot = g_theGxDevicePtr->TerrainTextureSlot(gx);

            if (slot != ~0u) {
                GxTerrainDraw draw = {};
                draw.world = world;
                draw.tex0 = ident;
                draw.tex1 = ident;
                draw.layerCount = 1;
                draw.layerTex[0] = slot;
                draw.layerTex[1] = ~0u;
                draw.layerTex[2] = ~0u;
                draw.layerTex[3] = ~0u;
                draw.layerAlpha[0] = ~0u;
                draw.layerAlpha[1] = ~0u;
                draw.layerAlpha[2] = ~0u;
                draw.layerAlpha[3] = ~0u;
                draw.indexCount = tile.indexCount;
                draw.firstIndex = tile.firstIndex;
                draw.baseVertex = static_cast<int32_t>(tile.baseVertex);
                batch.push_back(draw);

                if (!bindVerts) {
                    bindVerts = tile.verts;
                    bindIndices = tile.indices;
                    bindTex = gx;
                }

                continue;
            }
        }

        if (gx) {
            GxTexSetWrap(gx, GxTex_Clamp, GxTex_Clamp);
            GxRsSet(GxRs_ColorOp0, GxTexOp_Mod2x);
            GxRsSet(GxRs_Texture0, static_cast<void*>(gx));
        } else {
            GxRsSet(GxRs_ColorOp0, GxTexOp_Mod);
            GxRsSet(GxRs_Texture0, static_cast<void*>(nullptr));
        }

        GxPrimVertexPtr(tile.verts, GxVBF_PCT);
        g_theGxDevicePtr->PrimIndexPtr(tile.indices);

        CGxBatch draw;
        draw.m_primType = GxPrim_Triangles;
        draw.m_start = 0;
        draw.m_count = tile.indexCount;
        draw.m_minIndex = 0;
        draw.m_maxIndex = kVerts - 1;
        g_theGxDevicePtr->Draw(&draw, 1);
    }

    // ʕ •ᴥ•ʔ✿ One indirect draw for the whole continent. A draw per map tile was the overhead view ✿ ʕ •ᴥ•ʔ
    if (!batch.empty() && bindVerts && bindTex) {
        // ʕ •ᴥ•ʔ✿ The stage stays off with a null texture, and the shader then draws vertex color only ✿ ʕ •ᴥ•ʔ
        GxRsSet(GxRs_ColorOp0, GxTexOp_Mod2x);
        GxRsSet(GxRs_Texture0, static_cast<void*>(bindTex));
        GxPrimVertexPtr(bindVerts, GxVBF_PCT);
        g_theGxDevicePtr->PrimIndexPtr(bindIndices);
        g_theGxDevicePtr->DrawTerrainBatch(batch.data(), static_cast<uint32_t>(batch.size()), 2);
    }

    GxRsSet(GxRs_Texture0, static_cast<CTexture*>(nullptr));
}

// VERIFY: c20afbce manual
// OFFSET: 0x007D5E70
void CMap::RenderLowDetail(CMapAreaLow* area) {
    if (static_cast<int32_t>(area->m_indexCount) <= 0) {
        return;
    }

    if (!area->m_block) {
        CMap::s_vbbList.AcquireBlock(&area->m_block);
    }

    if (area->m_block) {
        CGxBuf* buf = area->m_block->m_buf;

        if (!buf->m_hasData || !buf->m_valid) {
            CMap::BuildLowDetailGridVertices(area, buf);
        }

        area->m_block->m_lastUsedFrame = CWorld::frameCnt;
        GxPrimVertexPtr(area->m_block->m_buf, GxVBF_PC);
    } else {
        CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 16, 545);
        CMap::BuildLowDetailGridVertices(area, buf);
        GxPrimVertexPtr(buf, GxVBF_PC);
    }

    uint32_t count;

    if (!(area->m_flags & 0x1) && area->m_indexCount == 3072) {
        GxRsSet(GxRs_Culling, 0);
        CGxBuf* indexBuf = CMap::lowDetailIndexBuf;

        if (!indexBuf->m_hasData || !indexBuf->m_valid) {
            CMap::BuildLowDetailGridIndices(nullptr, 0, CMap::lowDetailIndexBuf);
            indexBuf = CMap::lowDetailIndexBuf;
        }

        g_theGxDevicePtr->PrimIndexPtr(indexBuf);
        count = 3072;
    } else {
        GxRsSet(GxRs_Culling, 0);
        CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, area->m_indexCount);
        CMap::BuildLowDetailGridIndices(area->m_holes, 0, indexBuf);
        g_theGxDevicePtr->PrimIndexPtr(indexBuf);

        CGxBatch batch;
        batch.m_primType = GxPrim_Triangles;
        batch.m_start = 0;
        batch.m_count = area->m_indexCount;
        batch.m_minIndex = 0;
        batch.m_maxIndex = 544;
        g_theGxDevicePtr->Draw(&batch, 1);

        GxRsSet(GxRs_Culling, 1);
        indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, 3072 - area->m_indexCount);
        CMap::BuildLowDetailGridIndices(area->m_holes, 1, indexBuf);
        g_theGxDevicePtr->PrimIndexPtr(indexBuf);
        count = 3072 - area->m_indexCount;
    }

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = count;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 544;
    g_theGxDevicePtr->Draw(&batch, 1);
}

// VERIFY: 894a0529 manual
// OFFSET: 0x007D5150
void CMap::BuildLowDetailGridVertices(CMapAreaLow* area, CGxBuf* buf) {
    CGxVertexPC* vertex = reinterpret_cast<CGxVertexPC*>(g_theGxDevicePtr->BufLock(buf));
    uint32_t index = 0;
    float x = area->m_topLeft.x;

    for (int32_t row = 0; row < 17; row++) {
        float y = area->m_topLeft.y;

        for (int32_t col = 0; col < 17; col++) {
            vertex->p.x = x;
            vertex->p.y = y;
            vertex->p.z = area->m_heights[index++];
            vertex->c = CImVector(0xFF, 0xFF, 0xFF, 0xFF);
            vertex++;
            y -= 33.333332f;
        }

        x -= 33.333332f;
    }

    x = area->m_topLeft.x - 16.666666f;

    for (int32_t row = 0; row < 16; row++) {
        float y = area->m_topLeft.y - 16.666666f;

        for (int32_t col = 0; col < 16; col++) {
            vertex->p.x = x;
            vertex->p.y = y;
            vertex->p.z = area->m_heights[index++];
            vertex->c = CImVector(0xFF, 0xFF, 0xFF, 0xFF);
            vertex++;
            y -= 33.333332f;
        }

        x -= 33.333332f;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
}

// VERIFY: 7d2e0b07 manual
// OFFSET: 0x007D5240
void CMap::BuildLowDetailGridIndices(uint16_t* holes, int32_t pass, CGxBuf* buf) {
    uint16_t* indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(buf));
    uint32_t outer = 0;
    uint32_t inner = 289;

    for (uint32_t row = 0; row < 16; row++) {
        for (int32_t col = 0; col < 16; col++) {
            if (!holes || (pass ? (holes[row] & (1 << col)) != 0 : (holes[row] & (1 << col)) == 0)) {
                *indices++ = inner;
                *indices++ = outer + 1;
                *indices++ = outer;
                *indices++ = inner;
                *indices++ = outer + 18;
                *indices++ = outer + 1;
                *indices++ = inner;
                *indices++ = outer + 17;
                *indices++ = outer + 18;
                *indices++ = inner;
                *indices++ = outer;
                *indices++ = outer + 17;
            }

            outer++;
            inner++;
        }

        outer++;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->m_hasData = 1;
}

// VERIFY: f803b515 manual
// OFFSET: 0x007CC770
void CMapLowDetail::Unload() {
    for (uint32_t i = 0; i < 4096; i++) {
        if (this->m_areas[i]) {
            CMap::FreeAreaLow(this->m_areas[i]);
            this->m_areas[i] = nullptr;
        }
    }

    for (uint32_t i = 0; i < this->m_mapObjDefList.m_count; i++) {
        this->m_mapObjDefList.m_data[i]->m_refCount--;
        CMap::PurgeMapObjDef(this->m_mapObjDefList.m_data[i]);
    }

    this->m_mapObjDefList.m_count = 0;
    this->m_mapObjNames = nullptr;
    this->m_mapObjNameOffsets = nullptr;
    this->m_mapObjDefs = nullptr;
    this->m_mapObjDefCount = 0;
    this->m_areaOffsets = nullptr;

    if (this->m_data) {
        SMemFree(this->m_data, nullptr, 0, 0);
        this->m_data = nullptr;
    }
}
