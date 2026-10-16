#include "MapRenderChunk.h"
#include <cmath>
#include <cstring>
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Profiler.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "Engine/Source/Tempest/C33Matrix.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapArea.h"
#include "WoW/Source/WorldClient/MapChunk.h"
#include "WoW/Source/WorldClient/MapLight.h"
#include "WoW/Source/WorldClient/Types.h"
#include "WoW/Source/WorldClient/World.h"
#include "Engine/Source/Base/Job/JobSystem.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#else
#include <alloca.h>
#endif

// STATIC: 0x00AEEBF8
static uint32_t s_maxVertexCount = 145;

// STATIC: 0x00AEEC18
static uint16_t s_chunkBatchIndexCount = 768;

// STATIC: 0x00A3FFF4
static const uint32_t s_nibbleMask[2] = { 0x0F, 0xF0 };

// STATIC: 0x00A3FFFC
static const uint32_t s_nibbleShift[2] = { 4, 0 };

// STATIC: 0x00A40004
static const uint32_t s_nibbleUnshift[2] = { 0, 4 };

// STATIC: 0x00A4000C
static const uint32_t s_bitMask[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

// STATIC: 0x00A4002C
static const uint32_t s_bitShift[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

// STATIC: 0x00A4004C
static const uint8_t s_shadowAlpha[2] = { 0xFF, 0x00 };

// STATIC: 0x00AEE214
static uint16_t s_shadowTexel[2] = { 0x0000, 0xFFFF };

// STATIC: 0x00AF14A0
static uint16_t s_streamVertexCount = 145;

// STATIC: 0x00AF14C0
static uint16_t s_streamIndexCount = 768;

// STATIC: 0x00AF14F8
static float s_texAnimRate[8] = { 64.0f, 48.0f, 32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f };

// STATIC: 0x00D1CED8
static uint8_t s_alphaRowBuf[4][64];

// STATIC: 0x00D1CFD8
static uint8_t s_defaultShadowRow[64];

// STATIC: 0x00D1D018
static uint8_t s_defaultAlphaRow[64];

// STATIC: 0x00AEEC20
static float s_layerChannel[4][4] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } };

// STATIC: 0x00AEEC70
TSExplicitList<CMapChunkBufBlock, offsetof(CMapChunkBufBlock, m_blockLink)> CMapRenderChunk::s_chunkBufBlockFreeList;

// STATIC: 0x00AEEC7C
TSExplicitList<CMapChunkBufBlock, offsetof(CMapChunkBufBlock, m_bufLink)> CMapRenderChunk::s_bufList;

// STATIC: 0x00AEEC88
TSExplicitList<CMapChunkBuf, offsetof(CMapChunkBuf, m_freeLink)> CMapRenderChunk::s_renderChunkBufFreeList;

// STATIC: 0x00AEEC60
CGxBatch CMapRenderChunk::s_chunkBatch;

// STATIC: 0x00D1D058
int32_t CMapRenderChunk::s_bPoolsDirty;

// STATIC: 0x00D1D05C
uint32_t CMapRenderChunk::s_pnEstimateIndex;

// STATIC: 0x00D1D060
uint32_t CMapRenderChunk::s_pnEstimateVertex;

// STATIC: 0x00D1D064
CGxPool* CMapRenderChunk::s_gxIndexPool;

// STATIC: 0x00D1D068
CGxPool* CMapRenderChunk::s_gxVertexPool;

static std::vector<CGxPool*> s_vertexSlabs;
static std::vector<CGxPool*> s_indexSlabs;
static uint32_t s_slotsPerSlab = 0;

// STATIC: 0x00D1D06C
EGxVertexBufferFormat CMapRenderChunk::s_gxBufVertexFormat;

// STATIC: 0x00D1D070
CGxShader* CMapRenderChunk::s_currentShader[4];

// STATIC: 0x00D1D080
CGxShader* CMapRenderChunk::s_currentShaderX[4];

// STATIC: 0x00D1D090
CGxShader* CMapRenderChunk::s_pixelShader[2];

// STATIC: 0x00D1D098
uint32_t CMapRenderChunk::s_defaultTex[8192];

// STATIC: 0x00D25098
CMapRenderChunk::RenderLayersFunc CMapRenderChunk::s_renderLayersFunc;

// STATIC: 0x00D2509C
CImVector CMapRenderChunk::s_fogColor;

// STATIC: 0x00D250A0
CMapRenderChunk::SVertexShaderConstants CMapRenderChunk::s_vertexShaderConsts;

// STATIC: 0x00D252F0
TSGrowableArray<CMapChunkBufBlock> CMapRenderChunk::s_chunkBlockArray;

namespace {

struct OwnedFill {
    CMapRenderChunk* chunk;
    char* vertices;
    uint16_t* indices;
    CGxBuf* vertexBuf;
    CGxBuf* indexBuf;
};

std::vector<CMapRenderChunk*> s_ownedFills;

bool RangesOverlap(const char* a, uint32_t aSize, const char* b, uint32_t bSize) {
    if (!a || !b || !aSize || !bSize) {
        return false;
    }

    uintptr_t a0 = reinterpret_cast<uintptr_t>(a);
    uintptr_t b0 = reinterpret_cast<uintptr_t>(b);
    return a0 < b0 + bSize && b0 < a0 + aSize;
}

void FillLockedBufs(CMapRenderChunk* chunk, char* vertices, uint16_t* indices) {
    if (vertices) {
        C3Vector offset;

        if (chunk->m_mapChunks[0]) {
            chunk->m_mapChunks[0]->CreateVertices(vertices, 0, offset);
        }

        if (chunk->m_mapChunks[1]) {
            offset.z = chunk->m_mapChunks[1]->m_topLeftCoords.z - chunk->m_mapChunks[0]->m_topLeftCoords.z;

            if (chunk->m_unkFlags & 0x2) {
                offset.y = -33.333332f;
            } else {
                offset.x = -33.333332f;
            }

            chunk->m_mapChunks[1]->CreateVertices(vertices, 145, offset);
        }
    }

    if (indices) {
        chunk->m_batch.m_primType = GxPrim_Triangles;
        chunk->m_batch.m_start = 0;
        chunk->m_batch.m_count = 0;
        chunk->m_batch.m_minIndex = 0xFFFF;
        chunk->m_batch.m_maxIndex = 0;

        if (chunk->m_mapChunks[0]) {
            chunk->m_mapChunks[0]->CreateIndices(indices, &chunk->m_batch);
        }

        if (chunk->m_mapChunks[1]) {
            chunk->m_mapChunks[1]->CreateIndices(&indices[chunk->m_batch.m_count], &chunk->m_batch);
        }
    }
}

void FillOwnedRange(Job::Range range, void* user) {
    OwnedFill* jobs = static_cast<OwnedFill*>(user);

    for (uint32_t i = range.begin; i < range.end; ++i) {
        FillLockedBufs(jobs[i].chunk, jobs[i].vertices, jobs[i].indices);
    }
}

void CancelOwnedFill(CMapRenderChunk* chunk) {
    for (size_t i = 0; i < s_ownedFills.size();) {
        if (s_ownedFills[i] == chunk) {
            s_ownedFills.erase(s_ownedFills.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void ClearOwnedFills() {
    s_ownedFills.clear();
}

void QueueOwnedFill(CMapRenderChunk* chunk) {
    if (!chunk || !chunk->m_chunkBuf) {
        return;
    }

    CGxBuf* vertexBuf = chunk->m_chunkBuf->m_vertexBuf;
    CGxBuf* indexBuf = chunk->m_chunkBuf->m_indexBuf;

    if (!vertexBuf || !indexBuf) {
        return;
    }

    if (vertexBuf->m_hasData && vertexBuf->m_valid && indexBuf->m_hasData && indexBuf->m_valid) {
        return;
    }

    for (CMapRenderChunk* queued : s_ownedFills) {
        if (queued == chunk) {
            return;
        }
    }

    s_ownedFills.push_back(chunk);
}

void FlushOwnedFills() {
    if (s_ownedFills.empty()) {
        return;
    }

    std::vector<CMapRenderChunk*> pending;
    pending.swap(s_ownedFills);

    if (!g_theGxDevicePtr) {
        s_ownedFills.swap(pending);
        return;
    }

    CMapChunk::SetVertexColorFormat(g_theGxDevicePtr->Caps().m_colorFormat);

    std::vector<OwnedFill> jobs;
    jobs.reserve(pending.size());
    bool overlap = false;

    for (CMapRenderChunk* chunk : pending) {
        if (!chunk->m_chunkBuf) {
            continue;
        }

        CGxBuf* vertexBuf = chunk->m_chunkBuf->m_vertexBuf;
        CGxBuf* indexBuf = chunk->m_chunkBuf->m_indexBuf;

        if (!vertexBuf || !indexBuf) {
            continue;
        }

        int32_t vertexReady = vertexBuf->m_hasData && vertexBuf->m_valid;
        int32_t indexReady = indexBuf->m_hasData && indexBuf->m_valid;

        if (vertexReady && indexReady) {
            continue;
        }

        OwnedFill job;
        job.chunk = chunk;
        job.vertices = nullptr;
        job.indices = nullptr;
        job.vertexBuf = vertexBuf;
        job.indexBuf = indexBuf;

        if (!vertexReady) {
            job.vertices = g_theGxDevicePtr->BufLock(vertexBuf);

            for (const OwnedFill& prev : jobs) {
                if (RangesOverlap(job.vertices, vertexBuf->m_size, prev.vertices, prev.vertexBuf->m_size)) {
                    overlap = true;
                }
            }
        }

        if (!indexReady) {
            job.indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));

            for (const OwnedFill& prev : jobs) {
                if (RangesOverlap(reinterpret_cast<char*>(job.indices), indexBuf->m_size, reinterpret_cast<char*>(prev.indices), prev.indexBuf->m_size)) {
                    overlap = true;
                }
            }
        }

        jobs.push_back(job);
    }

    if (jobs.empty()) {
        return;
    }

    if (overlap) {
        for (OwnedFill& job : jobs) {
            if (job.vertices) {
                g_theGxDevicePtr->BufUnlock(job.vertexBuf, 0);
            }

            if (job.indices) {
                g_theGxDevicePtr->BufUnlock(job.indexBuf, 0);
            }

            job.chunk->RenderPrepBufs(job.vertexBuf, job.indexBuf);
        }

        return;
    }

    Job::ParallelFor(static_cast<uint32_t>(jobs.size()), FillOwnedRange, jobs.data());

    for (OwnedFill& job : jobs) {
        if (job.vertices) {
            g_theGxDevicePtr->BufUnlock(job.vertexBuf, 0);
            job.vertexBuf->m_hasData = 1;
        }

        if (job.indices) {
            g_theGxDevicePtr->BufUnlock(job.indexBuf, 0);
            job.indexBuf->m_hasData = 1;
        }
    }
}

}

// VERIFY: b7c2a1ad auto
// OFFSET: 0x007B7320
void CMapRenderChunk::SetDirty() {
    CMapRenderChunk::s_bPoolsDirty = 1;
}

// VERIFY: b36ae9fd auto
// OFFSET: 0x007BA340
void CMapRenderChunk::Initialize() {
    Job::Init();
    CMapRenderChunk::s_bPoolsDirty = 1;
    CMapRenderChunk::s_gxBufVertexFormat = GxVBF_PN;
    CMapRenderChunk::s_chunkBatch.m_maxIndex = s_maxVertexCount - 1;
    CMapRenderChunk::s_pnEstimateVertex = 0;
    CMapRenderChunk::s_pnEstimateIndex = 0;
    CMapRenderChunk::s_chunkBatch.m_primType = GxPrim_Triangles;
    CMapRenderChunk::s_chunkBatch.m_count = s_chunkBatchIndexCount;
    CMapRenderChunk::s_chunkBatch.m_start = 0;
    CMapRenderChunk::s_chunkBatch.m_minIndex = 0;
    CMapRenderChunk::s_gxVertexPool = nullptr;
    CMapRenderChunk::s_gxIndexPool = nullptr;
    CMapRenderChunk::s_chunkBlockArray.SetCount(0);
    memset(s_defaultAlphaRow, 0, sizeof(s_defaultAlphaRow));
    memset(s_defaultShadowRow, 0, sizeof(s_defaultShadowRow));
}

struct SlabSlot {
    CGxPool* vertex;
    CGxPool* index;
    uint32_t local;
};

static uint32_t VertexStride() {
    return CMapRenderChunk::s_gxBufVertexFormat == GxVBF_PNC ? static_cast<uint32_t>(sizeof(CGxVertexPNC)) : static_cast<uint32_t>(sizeof(CGxVertexPN));
}

static uint32_t DiskSlots(int32_t radius) {
    if (radius < 1) {
        radius = 1;
    }

    uint64_t span = static_cast<uint64_t>(radius);
    uint64_t count = (2ull * span * span + 1ull) & ~1ull;
    const uint64_t continent = 1024ull * 1024ull;

    if (count > continent) {
        count = continent;
    }

    return static_cast<uint32_t>(count);
}

struct TerrainFit {
    int32_t radius;
    uint32_t slots;
    int32_t resident;
};

static TerrainFit FitTerrain() {
    int32_t radius = 1 - static_cast<int32_t>(World::s_farClip * -0.03f);

    if (radius < 1) {
        radius = 1;
    }

    // ʕ •ᴥ•ʔ✿ Chunk indices run 0..1023. A wider disk is off the continent ✿ ʕ •ᴥ•ʔ
    if (radius > 1023) {
        radius = 1023;
    }

    uint64_t heap = g_theGxDevicePtr ? g_theGxDevicePtr->ResidentHeapBytes() : 0;

    if (!heap) {
        if (radius > 160) {
            radius = 160;
        }

        return { radius, DiskSlots(radius), 0 };
    }

    uint64_t perSlot = 145ull * VertexStride() + 1536ull;
    uint64_t budget = heap - heap / 4;

    if (budget < (256ull << 20)) {
        budget = heap / 2;
    }

    uint64_t fitSlots = perSlot ? (budget / perSlot) & ~1ull : 0;

    if (fitSlots < 2) {
        fitSlots = 2;
    }

    uint32_t slots = DiskSlots(radius);

    if (slots > fitSlots) {
        slots = static_cast<uint32_t>(fitSlots > 0xffffffffull ? (0xffffffffull & ~1ull) : fitSlots);
        int32_t clamped = static_cast<int32_t>(std::sqrt(static_cast<double>(slots) * 0.5));

        if (clamped < 1) {
            clamped = 1;
        }

        if (clamped < radius) {
            radius = clamped;
        }

        uint32_t shrunk = DiskSlots(radius);

        if (shrunk < slots) {
            slots = shrunk;
        }
    }

    return { radius, slots, 1 };
}

static SlabSlot SlabSlotFor(uint32_t slot) {
    uint32_t slab = 0;
    uint32_t local = slot;

    if (s_slotsPerSlab) {
        slab = slot / s_slotsPerSlab;
        local = slot - slab * s_slotsPerSlab;
    }

    SlabSlot out;
    out.vertex = slab < s_vertexSlabs.size() ? s_vertexSlabs[slab] : CMapRenderChunk::s_gxVertexPool;
    out.index = slab < s_indexSlabs.size() ? s_indexSlabs[slab] : CMapRenderChunk::s_gxIndexPool;
    out.local = local;
    return out;
}

static int32_t IsVertexSlab(CGxPool* pool) {
    for (CGxPool* slab : s_vertexSlabs) {
        if (slab == pool) {
            return 1;
        }
    }

    return pool == CMapRenderChunk::s_gxVertexPool;
}

static int32_t IsIndexSlab(CGxPool* pool) {
    for (CGxPool* slab : s_indexSlabs) {
        if (slab == pool) {
            return 1;
        }
    }

    return pool == CMapRenderChunk::s_gxIndexPool;
}

int32_t CMapRenderChunk::DetailChunkRadius() {
    return FitTerrain().radius;
}

// VERIFY: 73f06b5c manual
// OFFSET: 0x007BA5A0
void CMapRenderChunk::Destroy() {
    CMapRenderChunk::DestroyBufs();
    CMapRenderChunk::s_pnEstimateVertex = 0;
    CMapRenderChunk::s_pnEstimateIndex = 0;
    CMapRenderChunk::s_bPoolsDirty = 1;

    if (g_theGxDevicePtr) {
        for (CGxPool* pool : s_vertexSlabs) {
            g_theGxDevicePtr->PoolDestroy(pool);
        }

        for (CGxPool* pool : s_indexSlabs) {
            g_theGxDevicePtr->PoolDestroy(pool);
        }
    }

    s_vertexSlabs.clear();
    s_indexSlabs.clear();
    s_slotsPerSlab = 0;
    CMapRenderChunk::s_gxVertexPool = nullptr;
    CMapRenderChunk::s_gxIndexPool = nullptr;
}

// VERIFY: 61c7e992 manual
// OFFSET: 0x007BA3D0
void CMapRenderChunk::DestroyBufs() {
    CMapChunkBuf* buf = CMapRenderChunk::s_renderChunkBufFreeList.Head();

    while (buf) {
        CMapChunkBuf* next = CMapRenderChunk::s_renderChunkBufFreeList.Next(buf);
        buf->m_freeLink.Unlink();
        buf = next;
    }

    CMapChunkBufBlock* block = CMapRenderChunk::s_chunkBufBlockFreeList.Head();

    while (block) {
        CMapChunkBufBlock* next = CMapRenderChunk::s_chunkBufBlockFreeList.Next(block);
        block->m_blockLink.Unlink();
        block->m_bufLink.Unlink();
        block = next;
    }

    for (uint32_t i = 0; i < CMapRenderChunk::s_chunkBlockArray.Count(); i++) {
        for (uint32_t j = 0; j < 2; j++) {
            CMapChunkBuf& chunkBuf = CMapRenderChunk::s_chunkBlockArray[i].m_bufs[j];

            if (chunkBuf.m_vertexBuf) {
                CGxBuf* vertexBuf = chunkBuf.m_vertexBuf;

                if (vertexBuf != g_theGxDevicePtr->BufStream(vertexBuf->m_pool->m_target, 0, 0)) {
                    g_theGxDevicePtr->BufDestroy(vertexBuf);
                }

                chunkBuf.m_vertexBuf = nullptr;
            }

            if (chunkBuf.m_indexBuf) {
                CGxBuf* indexBuf = chunkBuf.m_indexBuf;

                if (indexBuf != g_theGxDevicePtr->BufStream(indexBuf->m_pool->m_target, 0, 0)) {
                    g_theGxDevicePtr->BufDestroy(indexBuf);
                }

                chunkBuf.m_indexBuf = nullptr;
            }
        }
    }

    CMapRenderChunk::s_chunkBlockArray.SetCount(0);
}

// INLINED
CMapChunkBuf::CMapChunkBuf() {
    this->m_vertexBuf = nullptr;
    this->m_indexBuf = nullptr;
    this->m_renderChunk = nullptr;
    this->m_block = nullptr;
    this->m_unkFlags = 0;
}

// INLINED
CMapChunkBufBlock::CMapChunkBufBlock() {
    this->m_vertexCount = 0;
}

// VERIFY: 1baa8ad1 auto
// OFFSET: 0x007B7330
void CMapRenderChunk::SetVertexFormat(EGxVertexBufferFormat format) {
    CMapRenderChunk::s_gxBufVertexFormat = format;
    CMapRenderChunk::s_bPoolsDirty = 1;
}

// VERIFY: 75114f19 auto
// OFFSET: 0x007B7350
void CMapRenderChunk::FreeLayers() {
    for (uint32_t i = 0; i < this->m_layersCount; i++) {
        CMapRenderChunkLayer& layer = this->m_layers[i];
        layer.m_texture = nullptr;
        layer.m_textureId = -1;
        layer.m_flags = 0;

        if (layer.m_layerTexture) {
            HandleClose(reinterpret_cast<HOBJECT>(layer.m_layerTexture));
            layer.m_layerTexture = nullptr;
        }
    }

    this->m_layersCount = 0;

    if (this->m_shadowTexture) {
        HandleClose(reinterpret_cast<HOBJECT>(this->m_shadowTexture));
        this->m_shadowTexture = nullptr;
    }

    if (this->m_terrainBlendTexture) {
        HandleClose(reinterpret_cast<HOBJECT>(this->m_terrainBlendTexture));
        this->m_terrainBlendTexture = nullptr;
    }
}

// VERIFY: 6909fb20 auto
// OFFSET: 0x007B73E0
void CMapRenderChunk::UpdateLoaded() {
    for (int32_t i = 0; i < this->m_layersCount; i++) {
        if (!TextureGetGxTex(this->m_layers[i].m_texture, 0, nullptr)) {
            return;
        }
    }

    this->m_unkFlags |= 0x8;
}

// VERIFY: ef5458ef auto
// OFFSET: 0x007B7A70
CTexture* CMapRenderChunk::AllocTexture(uint32_t width, uint32_t height, void* userArg, GxTexUserFunc userFunc, EGxTexFormat format, EGxTexFormat dataFormat) {
    EGxTexFormat texFormat;

    if (CMap::header.m_flags & 0x4) {
        texFormat = static_cast<EGxTexFormat>((CWorld::terrainAlphaBitDepth != 8) + 2);
    } else {
        texFormat = format;
    }

    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
    flags.m_filter = GxTex_Linear;

    return TextureCreate(GxTex_2d, width, height, 0, texFormat, dataFormat, flags, userArg, userFunc, "TerrainBlend", 0);
}

// VERIFY: 686076da auto
// OFFSET: 0x007B7AF0
void CMapRenderChunk::AddBatch(CMapChunk* chunk1, CMapChunk* chunk2, C3Vector* position, uint8_t flags) {
    this->m_mapChunks[0] = chunk1;
    this->m_mapChunks[1] = chunk2;
    this->m_vec1 = *position;
    this->m_unkFlags = flags;

    CAaBox box = chunk1->m_bbox;

    if (chunk2) {
        box |= chunk2->m_bbox;
    }

    C3Vector diagonal = box.t - box.b;
    this->m_sphere.r = sqrtf(diagonal.SquaredMag()) * 0.5f;
    C3Vector center = box.t + box.b;
    center *= 0.5f;
    this->m_sphere.c = center;
}

// VERIFY: c9761f91 auto
// OFFSET: 0x007B7BD0
void CMapRenderChunk::SelectLights(CM2Lighting* lighting) {
    lighting->AddLight(&CMap::s_mapLight->m_light);

    DayNight::DNInfo* info = DayNight::GetInfo();
    C3Vector fogColor;
    fogColor.x = info->m_fog.m_color.r * 0.0039215689f;
    fogColor.y = info->m_fog.m_color.g * 0.0039215689f;
    fogColor.z = info->m_fog.m_color.b * 0.0039215689f;

    lighting->SetFog(fogColor, info->m_fog.m_start, info->m_fog.m_end, info->m_fog.m_density);
}

// VERIFY: bc2de872 manual
// OFFSET: 0x007B9250
void CMapRenderChunk::CreateLayer(CMapArea* area, SMLayer* layer, int32_t checkDuplicate) {
    if (checkDuplicate) {
        for (int32_t i = 0; i < this->m_layersCount; i++) {
            if (this->m_layers[i].m_textureId == layer->m_textureId) {
                return;
            }
        }
    }

    CMapRenderChunkLayer* renderLayer = &this->m_layers[this->m_layersCount];
    renderLayer->m_flags = layer->m_flags;
    renderLayer->m_layerIndex = this->m_layersCount;

    CMapAreaTexture* areaTexture = &area->m_textures[layer->m_textureId];

    if (!areaTexture->m_texture) {
        area->LoadTerrainTexture(areaTexture, layer->m_textureId);
    }

    renderLayer->m_texture = areaTexture->m_texture;
    renderLayer->m_textureId = layer->m_textureId;
    renderLayer->m_layerTexture = nullptr;
    renderLayer->m_owner = this;

    if (renderLayer->m_flags & 0x80) {
        this->m_renderFlags |= 0x1;
    }

    if (g_theGxDevicePtr->Caps().m_texTarget[1] && g_theGxDevicePtr->Caps().m_shaderTargets[0] && g_theGxDevicePtr->Caps().m_shaderTargets[4] && (renderLayer->m_flags & 0x400)) {
        this->m_renderFlags |= 0x4;
    }

    this->m_layersCount++;
}

// VERIFY: 6c5dc3eb manual
// OFFSET: 0x007B9340
CMapChunkBuf* CMapRenderChunk::AllocBuf(int32_t flags, CMapRenderChunk* renderChunk) {
    if (flags & 0x3) {
        CMapChunkBufBlock* block = CMapRenderChunk::s_chunkBufBlockFreeList.Head();

        if (!block) {
            return nullptr;
        }

        block->m_blockLink.Unlink();

        CMapChunkBuf* buf = &block->m_bufs[0];
        buf->m_renderChunk = renderChunk;
        buf->m_unkFlags = flags;

        if (buf->m_vertexBuf) {
            buf->m_vertexBuf->m_hasData = 0;
            buf->m_indexBuf->m_hasData = 0;
            return buf;
        }

        uint32_t stride = VertexStride();
        SlabSlot slot = SlabSlotFor(block->m_vertexCount);
        buf->m_vertexBuf = g_theGxDevicePtr->BufCreate(slot.vertex, stride, 290, 145 * stride * slot.local);
        buf->m_indexBuf = g_theGxDevicePtr->BufCreate(slot.index, 2, 1536, 1536 * slot.local);
        return buf;
    }

    CMapChunkBuf* freeBuf = CMapRenderChunk::s_renderChunkBufFreeList.Tail();

    if (freeBuf) {
        freeBuf->m_freeLink.Unlink();
        freeBuf->m_renderChunk = renderChunk;
        freeBuf->m_unkFlags = flags;
        freeBuf->m_vertexBuf->m_hasData = 0;
        freeBuf->m_indexBuf->m_hasData = 0;
        return freeBuf;
    }

    CMapChunkBufBlock* block = CMapRenderChunk::s_chunkBufBlockFreeList.Tail();

    if (!block) {
        return nullptr;
    }

    block->m_blockLink.Unlink();

    if (block->m_bufs[0].m_vertexBuf) {
        GxBufDestroy(block->m_bufs[0].m_vertexBuf);
        GxBufDestroy(block->m_bufs[0].m_indexBuf);
        block->m_bufs[0].m_vertexBuf = nullptr;
        block->m_bufs[0].m_indexBuf = nullptr;
    }

    uint32_t stride = VertexStride();
    CMapChunkBuf* buf;

    for (int32_t i = 0; i < 2; i++) {
        buf = &block->m_bufs[i];
        SlabSlot slot = SlabSlotFor(block->m_vertexCount + i);
        buf->m_vertexBuf = g_theGxDevicePtr->BufCreate(slot.vertex, stride, 145, 145 * stride * slot.local);
        buf->m_indexBuf = g_theGxDevicePtr->BufCreate(slot.index, 2, 768, 1536 * slot.local);
        buf->m_unkFlags = 0;
        buf->m_renderChunk = nullptr;

        if (i == 0) {
            CMapRenderChunk::s_renderChunkBufFreeList.LinkToTail(buf);
        }
    }

    buf->m_renderChunk = renderChunk;
    buf->m_unkFlags = flags;
    return buf;
}

// VERIFY: e18a2023 manual
// OFFSET: 0x007B9560
void CMapRenderChunk::PurgeChunkBufBlocks() {
    CMapChunk::FlushQueuedVisibility();

    for (uint32_t i = 0; i < CMapRenderChunk::s_chunkBlockArray.Count(); i++) {
        CMapChunkBufBlock& block = CMapRenderChunk::s_chunkBlockArray[i];

        if (block.m_blockLink.IsLinked() || !block.m_bufs[0].m_freeLink.IsLinked() || !block.m_bufs[1].m_freeLink.IsLinked()) {
            continue;
        }

        for (uint32_t j = 0; j < 2; j++) {
            CMapChunkBuf& chunkBuf = block.m_bufs[j];
            CGxBuf* vertexBuf = chunkBuf.m_vertexBuf;

            if (vertexBuf && vertexBuf != g_theGxDevicePtr->BufStream(vertexBuf->m_pool->m_target, 0, 0)) {
                g_theGxDevicePtr->BufDestroy(vertexBuf);
            }

            CGxBuf* indexBuf = chunkBuf.m_indexBuf;

            if (indexBuf && indexBuf != g_theGxDevicePtr->BufStream(indexBuf->m_pool->m_target, 0, 0)) {
                g_theGxDevicePtr->BufDestroy(indexBuf);
            }

            chunkBuf.m_vertexBuf = nullptr;
            chunkBuf.m_indexBuf = nullptr;
            chunkBuf.m_unkFlags = 0;
            chunkBuf.m_freeLink.Unlink();
        }

        CMapRenderChunk::s_chunkBufBlockFreeList.LinkToHead(&block);
    }
}

// VERIFY: 92303a74 manual
// OFFSET: 0x007B9690
CMapRenderChunk::CMapRenderChunk() {
    this->m_sphere.c = C3Vector();
    this->m_sphere.r = 0.0f;
    this->m_batch.m_primType = GxPrims_Last;
    this->m_batch.m_start = 0;
    this->m_batch.m_count = 0;
    this->m_batch.m_minIndex = 0;
    this->m_batch.m_maxIndex = 0;
    this->m_lastUpdateTime = 0.0f;
    this->m_unkFlags = 0;
    this->m_mapChunks[0] = nullptr;
    this->m_mapChunks[1] = nullptr;
    this->m_layersCount = 0;
    this->m_renderFlags = 0;
    this->m_terrainBlendTexture = nullptr;
    this->m_shadowTexture = nullptr;
    this->m_chunkBuf = nullptr;
}

// VERIFY: fad72c2f manual
// OFFSET: 0x007B9770
void CMapRenderChunk::CreateLayers() {
    int32_t chunkCount = 1;

    if (CMap::header.m_flags & 0x4) {
        chunkCount = 2;
    }

    for (int32_t i = 0; i < chunkCount; i++) {
        CMapChunk* chunk = this->m_mapChunks[i];

        if (!chunk) {
            continue;
        }

        CMapArea* area = static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref);
        SMChunk* header = chunk->m_header;

        if (!header->m_nLayers) {
            continue;
        }

        SMLayer* layer = chunk->m_layers;

        for (uint32_t j = 0; j < header->m_nLayers; j++) {
            this->CreateLayer(area, layer, i != 0);
            layer++;
        }
    }

    this->m_renderFlags |= 0x20;
}

// VERIFY: eb3e1413 auto
// OFFSET: 0x007B9830
void CMapRenderChunk::FreeBuf() {
    CMapChunkBuf* chunkBuf = this->m_chunkBuf;

    if (!chunkBuf) {
        return;
    }

    if (chunkBuf->m_unkFlags & 0x3) {
        CMapRenderChunk::s_chunkBufBlockFreeList.LinkToHead(chunkBuf->m_block);
    } else {
        CMapRenderChunk::s_renderChunkBufFreeList.LinkToHead(this->m_chunkBuf);
    }

    chunkBuf->m_renderChunk = nullptr;
    this->m_chunkBuf = nullptr;
}

// VERIFY: c324fde5 manual
// OFFSET: 0x007B9CB0
CMapChunkBufBlock::~CMapChunkBufBlock() {
}

// VERIFY: a8d77235 manual
// OFFSET: 0x007B9D60
CMapRenderChunk::~CMapRenderChunk() {
    CancelOwnedFill(this);
    this->FreeBuf();
    this->FreeLayers();

    if (this->m_mapChunks[0]) {
        this->m_mapChunks[0]->m_renderChunk = nullptr;
        this->m_mapChunks[0]->m_bLoaded = 0;
        this->m_mapChunks[0] = nullptr;
    }

    if (this->m_mapChunks[1]) {
        this->m_mapChunks[1]->m_renderChunk = nullptr;
        this->m_mapChunks[1]->m_bLoaded = 0;
        this->m_mapChunks[1] = nullptr;
    }
}

// VERIFY: 2d552482 manual
// OFFSET: 0x007BA600
void CMapRenderChunk::UpdatePools() {
    CMapChunk::FlushQueuedVisibility();

    if (CMapRenderChunk::s_bPoolsDirty) {
        ClearOwnedFills();
    } else {
        FlushOwnedFills();
    }

    if (!CMapRenderChunk::s_bPoolsDirty) {
        return;
    }

    CMap::ClearAllRenderChunks();
    CMapRenderChunk::Destroy();

    TerrainFit fit = FitTerrain();
    uint32_t stride = VertexStride();
    uint32_t count = fit.slots;

    if (count < 2) {
        count = 2;
    }

    CMapRenderChunk::s_pnEstimateVertex = 145 * count;
    CMapRenderChunk::s_pnEstimateIndex = 768 * count;

    // ʕ •ᴥ•ʔ✿ One CGxPool size is a signed 32-bit byte count. Slabs stay under that and under a driver-sized alloc ✿ ʕ •ᴥ•ʔ
    const uint32_t kSlabBytes = 768u * 1024u * 1024u;
    uint32_t vertexPer = 145u * stride;
    uint32_t indexPer = 1536u;
    uint32_t slotsPer = vertexPer ? kSlabBytes / vertexPer : 2u;
    uint32_t indexSlots = indexPer ? kSlabBytes / indexPer : slotsPer;

    if (indexSlots < slotsPer) {
        slotsPer = indexSlots;
    }

    slotsPer &= ~1u;

    if (slotsPer < 2) {
        slotsPer = 2;
    }

    s_slotsPerSlab = slotsPer;
    uint32_t remaining = count;

    while (remaining) {
        uint32_t n = remaining > slotsPer ? slotsPer : remaining;
        n &= ~1u;

        if (!n) {
            break;
        }

        CGxPool* vertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, static_cast<int32_t>(n * vertexPer), GxPoolHintBit_Unk3, "CMapRenderChunk_vtx");
        CGxPool* indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Dynamic, static_cast<int32_t>(n * indexPer), GxPoolHintBit_Unk3, "CMapRenderChunk_idx");
        vertexPool->m_resident = fit.resident;
        indexPool->m_resident = fit.resident;
        s_vertexSlabs.push_back(vertexPool);
        s_indexSlabs.push_back(indexPool);
        remaining -= n;
    }

    CMapRenderChunk::s_gxVertexPool = s_vertexSlabs.empty() ? nullptr : s_vertexSlabs[0];
    CMapRenderChunk::s_gxIndexPool = s_indexSlabs.empty() ? nullptr : s_indexSlabs[0];

    std::fprintf(stderr, "terrain resident: radius=%d slots=%u slabs=%zu heapMiB=%llu\n",
        fit.radius,
        count,
        s_vertexSlabs.size(),
        static_cast<unsigned long long>((g_theGxDevicePtr ? g_theGxDevicePtr->ResidentHeapBytes() : 0) >> 20));

    CMapRenderChunk::s_chunkBlockArray.SetCount(count / 2);

    for (uint32_t i = 0; i < CMapRenderChunk::s_chunkBlockArray.Count(); i++) {
        CMapChunkBufBlock* block = &CMapRenderChunk::s_chunkBlockArray[i];
        block->m_vertexCount = 2 * i;
        block->m_bufs[0].m_block = block;
        block->m_bufs[1].m_block = block;
        CMapRenderChunk::s_chunkBufBlockFreeList.LinkToTail(block);
        CMapRenderChunk::s_bufList.LinkToTail(block);
    }

    CMapRenderChunk::s_bPoolsDirty = 0;
}

// VERIFY: 8aa8ada7 manual
// OFFSET: 0x007D02C0
void CMapRenderChunk::RenderPrepBufs(CGxBuf* vertexBuf, CGxBuf* indexBuf) {
    int32_t vertexReady = vertexBuf->m_hasData && vertexBuf->m_valid;
    int32_t indexReady = indexBuf->m_hasData && indexBuf->m_valid;

    if (vertexReady && indexReady) {
        return;
    }

    CMapChunk::SetVertexColorFormat(g_theGxDevicePtr->Caps().m_colorFormat);

    char* vertices = nullptr;
    uint16_t* indices = nullptr;

    if (!vertexReady) {
        vertices = g_theGxDevicePtr->BufLock(vertexBuf);
    }

    if (!indexReady) {
        indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));
    }

    FillLockedBufs(this, vertices, indices);

    if (!vertexReady) {
        g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
        vertexBuf->m_hasData = 1;
    }

    if (!indexReady) {
        g_theGxDevicePtr->BufUnlock(indexBuf, 0);
        indexBuf->m_hasData = 1;
    }
}

// VERIFY: 28de08e7 manual
// OFFSET: 0x007D0420
void CMapRenderChunk::UseStreamingBufs() {
    uint32_t stride = CMapRenderChunk::s_gxBufVertexFormat == GxVBF_PNC ? sizeof(CGxVertexPNC) : sizeof(CGxVertexPN);
    uint32_t vertexCount = s_streamVertexCount;
    uint32_t indexCount = s_streamIndexCount;

    if (this->m_unkFlags & 0x3) {
        vertexCount *= 2;
        indexCount *= 2;
    }

    CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, stride, vertexCount);
    CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, indexCount);

    this->RenderPrepBufs(vertexBuf, indexBuf);

    GxPrimVertexPtr(vertexBuf, CMapRenderChunk::s_gxBufVertexFormat);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);
}

// VERIFY: fdc9dc29 manual
// OFFSET: 0x007D04A0
void CMapRenderChunk::RenderSetup(int32_t a2) {
    FlushOwnedFills();
    this->m_lastUpdateTime = 0.0f;
    this->AllocLayerTextures();

    if (!a2 || !CMap::enableTerrainShaderVertex) {
        C44Matrix world;
        C3Vector translation = this->m_vec1 - CWorldScene::s_activeWorldView;
        world.d0 = translation.x;
        world.d1 = translation.y;
        world.d2 = translation.z;
        GxXformSet(GxXform_World, world);

        CM2Lighting lighting(this->m_sphere);
        World::s_m2Scene->SelectLights(&lighting);
        this->SelectLights(&lighting);
        lighting.SetupGxLights(&CWorldScene::s_activeWorldView);
        lighting.SetupGxFog();
    }

    if (this->m_chunkBuf) {
        GxPrimVertexPtr(this->m_chunkBuf->m_vertexBuf, CMapRenderChunk::s_gxBufVertexFormat);
        g_theGxDevicePtr->PrimIndexPtr(this->m_chunkBuf->m_indexBuf);
    } else {
        this->UseStreamingBufs();
    }
}

// VERIFY: 9923f02a manual
// OFFSET: 0x007D3010
void CMapRenderChunk::RenderSolid() {
    C44Matrix tex0;
    GxXformSet(GxXform_Tex0, tex0);
    C44Matrix tex1;
    GxXformSet(GxXform_Tex1, tex1);

    GxRsSet(GxRs_BlendingMode, 0);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    CGxTex* gxTex = TextureGetGxTex(CWorldScene::s_defaultTexture, 1, nullptr);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

    if (CMap::gTerrainPixelShadersValid) {
        g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(CWorldScene::s_defaultBlendTexture, 1, nullptr));
    } else {
        g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(CWorldScene::s_defaultTexture, 1, nullptr));
    }

    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: b2fa1daf auto
// OFFSET: 0x007D3230
void CMapRenderChunk::RenderSolidPixelShader() {
    this->RenderSolid();
}

// VERIFY: 90367efa manual
// OFFSET: 0x007D3240
void CMapRenderChunk::RenderSolidVertexPixelShader() {
    GxRsSet(GxRs_BlendingMode, 0);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    CGxTex* gxTex = TextureGetGxTex(CWorldScene::s_defaultTexture, 1, nullptr);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);
    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(CWorldScene::s_defaultBlendTexture, 1, nullptr));

    CMapRenderChunk::SetVertexShader(this, 2, 0);
    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: 6e2aac38 auto
// OFFSET: 0x007D3F70
void CMapRenderChunk::RenderPrep() {
    if (!this->m_chunkBuf) {
        this->m_chunkBuf = CMapRenderChunk::AllocBuf(this->m_unkFlags & 0x3, this);
    }

    if (this->m_chunkBuf) {
        CMapRenderChunk::s_bufList.LinkToHead(this->m_chunkBuf->m_block);
        // -- ʕ •ᴥ•ʔ✿ Main thread owns the buffer; workers fill it before the draw ✿ ʕ •ᴥ•ʔ
        QueueOwnedFill(this);
    }

    if (!this->m_layersCount) {
        this->CreateLayers();
    }

    if (!(this->m_unkFlags & 0x8)) {
        this->UpdateLoaded();
    }
}

// VERIFY: 5ffc93a3 manual
// OFFSET: 0x007D40A0
void CMapRenderChunk::RenderSinglePass() {
    g_theGxDevicePtr->RsPush();
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_VertexShader, 0);
    GxRsSet(GxRs_PixelShader, 0);

    uint32_t vertexCount = this->m_mapChunks[1] ? 290 : 145;
    CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, sizeof(CGxVertexPC), 2 * vertexCount);
    CGxVertexPC* vertices = reinterpret_cast<CGxVertexPC*>(g_theGxDevicePtr->BufLock(vertexBuf));
    CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, sizeof(uint16_t), 2 * vertexCount);
    uint16_t* indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));
    CGxVertexPN* source = static_cast<CGxVertexPN*>(alloca(sizeof(CGxVertexPN) * 145));
    uint16_t index = 0;

    for (uint32_t i = 0; i < 2; i++) {
        CMapChunk* chunk = this->m_mapChunks[i];

        if (!chunk) {
            continue;
        }

        chunk->CreateVerticesWorld(source, C3Vector(0.0f, 0.0f, 0.0f));

        for (uint32_t j = 0; j < 145; j++) {
            vertices[0].p = source[j].m_position;
            vertices[0].c = CImVector(0x00, 0xFF, 0xFF, 0xFF);
            vertices[1].p.x = source[j].m_normal.x * 0.75f + source[j].m_position.x;
            vertices[1].p.y = source[j].m_normal.y * 0.75f + source[j].m_position.y;
            vertices[1].p.z = source[j].m_normal.z * 0.75f + source[j].m_position.z;
            vertices[1].c = CImVector(0x00, 0xFF, 0xFF, 0xFF);
            vertices += 2;
            indices[0] = index;
            indices[1] = index + 1;
            indices += 2;
            index += 2;
        }
    }

    g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
    vertexBuf->m_hasData = 1;
    GxPrimVertexPtr(vertexBuf, GxVBF_PC);
    g_theGxDevicePtr->BufUnlock(indexBuf, 0);
    indexBuf->m_hasData = 1;
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    CGxBatch batch;
    batch.m_primType = GxPrim_Lines;
    batch.m_start = 0;
    batch.m_count = 2 * vertexCount;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(2 * vertexCount);
    g_theGxDevicePtr->Draw(&batch, 1);
    g_theGxDevicePtr->RsPop();
}

// INLINED
CMapRenderChunkLayer::CMapRenderChunkLayer() {
    this->m_flags = 0;
    this->m_layerIndex = 0;
    this->m_texture = nullptr;
    this->m_textureId = -1;
    this->m_layerTexture = nullptr;
    this->m_owner = nullptr;
}

// VERIFY: d15abdb3 auto
// OFFSET: 0x007B7420
uint8_t* CMapRenderChunk::DecodeAlphaRLE(uint8_t* src, uint8_t* dst, int32_t count) {
    int32_t srcIndex = 0;
    int32_t dstIndex = 0;

    while (dstIndex < count) {
        int8_t header = static_cast<int8_t>(src[srcIndex]);
        srcIndex++;

        if (header < 0) {
            uint32_t runLength = header & 0x7F;
            uint8_t value = src[srcIndex];
            srcIndex++;

            if (runLength) {
                memset(&dst[dstIndex], value, runLength);
                dstIndex += runLength;
            }
        } else {
            for (uint32_t i = static_cast<uint8_t>(header); i; i--) {
                dst[dstIndex] = src[srcIndex];
                srcIndex++;
                dstIndex++;
            }
        }
    }

    return &src[srcIndex];
}

// VERIFY: 425713a6 auto
// OFFSET: 0x007B74A0
void CMapRenderChunk::SetupAlphaRows(uint8_t** shadowCursor, uint8_t** shadowRow, SLayerAlpha* layers, uint32_t size, uint8_t** alphaRows) {
    if (*shadowCursor) {
        *shadowRow = *shadowCursor;
        *shadowCursor += size >> 3;
    } else {
        *shadowRow = s_defaultShadowRow;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (!layers[i].m_alphaData) {
            alphaRows[i] = s_defaultAlphaRow;
        } else if (layers[i].m_flags & 0x200) {
            alphaRows[i] = s_alphaRowBuf[i];
            layers[i].m_alphaData = CMapRenderChunk::DecodeAlphaRLE(layers[i].m_alphaData, s_alphaRowBuf[i], size);
        } else {
            alphaRows[i] = layers[i].m_alphaData;
            layers[i].m_alphaData += size;
        }
    }
}

// VERIFY: 900e0dee auto
// OFFSET: 0x007B7530
void CMapRenderChunk::SetupAlphaRows4444(uint32_t size, uint8_t** shadowRow, uint8_t** alphaRows, SLayerAlpha* layers, uint8_t** shadowCursor) {
    if (*shadowCursor) {
        *shadowRow = *shadowCursor;
        *shadowCursor += size >> 3;
    } else {
        *shadowRow = s_defaultShadowRow;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (layers[i].m_alphaData) {
            alphaRows[i] = layers[i].m_alphaData;
            layers[i].m_alphaData += size >> 1;
        } else {
            alphaRows[i] = s_defaultAlphaRow;
        }
    }
}

// VERIFY: 25ebba41 manual
// OFFSET: 0x007B75B0
void __stdcall CMapRenderChunk::UnpackAlphaBitsFixed4444Mip0(uint16_t* dst, uint32_t size, SLayerAlpha* layer) {
    uint8_t* alphaData = layer->m_alphaData;
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            *dst = static_cast<uint16_t>((((alphaData[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleUnshift[i & 1]) << 12) | 0xFFF);
            dst++;
            i++;
        }
    }
}

// VERIFY: ede6e802 manual
// OFFSET: 0x007B7620
void __stdcall CMapRenderChunk::UnpackAlphaBitsFixed4444Mip1(uint16_t* dst, uint32_t size, SLayerAlpha* layer) {
    uint8_t* alphaData = layer->m_alphaData;
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y += 2) {
        for (uint32_t x = 0; x < size - 2; x += 2) {
            *dst = static_cast<uint16_t>((((alphaData[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleUnshift[i & 1]) << 12) | 0xFFF);
            dst++;
            i += 2;
        }

        i++;
        *dst = static_cast<uint16_t>((((alphaData[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleUnshift[i & 1]) << 12) | 0xFFF);
        dst++;
        i++;

        if (y == size - 4) {
            i += 64;
        }

        i += size;
    }
}

// VERIFY: 152a7dd5 manual
// OFFSET: 0x007B76F0
void __stdcall CMapRenderChunk::UnpackAlphaBitsUnfixed4444Mip0(uint16_t* dst, uint32_t size, SLayerAlpha* layer) {
    uint8_t* alphaData = layer->m_alphaData;
    uint32_t i = 0;
    uint16_t texel = 0xFFF;

    for (uint32_t y = 0; y < size - 1; y++) {
        for (uint32_t x = 0; x < size - 1; x++) {
            texel = static_cast<uint16_t>((((alphaData[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleUnshift[i & 1]) << 12) | 0xFFF);
            i++;
            *dst = texel;
            dst++;
        }

        *dst = texel;
        dst++;
        i++;
    }

    i -= size;

    for (uint32_t x = 0; x < size - 1; x++) {
        texel = static_cast<uint16_t>((((alphaData[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleUnshift[i & 1]) << 12) | 0xFFF);
        i++;
        *dst = texel;
        dst++;
    }

    *dst = texel;
}

// VERIFY: 8998a684 manual
// OFFSET: 0x007B77D0
void __stdcall CMapRenderChunk::UnpackAlphaBitsUnfixed4444Mip1(uint16_t* dst, uint32_t size, SLayerAlpha* layer) {
    uint8_t* alphaData = layer->m_alphaData;
    uint32_t i = 0;

    for (uint32_t y = 0; y < size - 1; y += 2) {
        for (uint32_t x = 0; x < size - 1; x += 2) {
            *dst = static_cast<uint16_t>((((alphaData[i >> 1] & s_nibbleMask[i & 1]) >> s_nibbleUnshift[i & 1]) << 12) | 0xFFF);
            dst++;
            i += 2;
        }

        i += size;
    }
}

// VERIFY: 3a09a0ac manual
// OFFSET: 0x007B7860
void CMapRenderChunk::CollectLayerAlphaPtrs(CMapChunk* chunk, SLayerAlpha* layers) {
    for (uint32_t i = 0; i < 4; i++) {
        layers[i].m_alphaData = nullptr;

        if (this->m_layersCount > i) {
            SMLayer* layer = &chunk->m_layers[i];

            if (layer->m_flags & 0x100) {
                layers[i].m_alphaData = &chunk->m_additionalShadowmap[layer->m_offsetInMCAL];
            }

            layers[i].m_flags = layer->m_flags;
        }
    }
}

// VERIFY: c03f753e manual
// OFFSET: 0x007B7930
void __stdcall CMapRenderChunk::ExpandShadowBitsToTexels(uint16_t* dst, uint32_t size, uint8_t* shadowMap) {
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y++) {
        for (uint32_t x = 0; x < size; x++) {
            *dst = s_shadowTexel[(shadowMap[i >> 3] & s_bitMask[i & 7]) >> s_bitShift[i & 7]];
            dst++;
            i++;
        }
    }
}

// VERIFY: e6e2d775 manual
// OFFSET: 0x007B79A0
void __stdcall CMapRenderChunk::ExpandShadowBitsToTexelsMip(uint16_t* dst, uint32_t size, uint8_t* shadowMap) {
    uint32_t i = 0;

    for (uint32_t y = 0; y < size; y += 2) {
        for (uint32_t x = 0; x < size - 2; x += 2) {
            *dst = s_shadowTexel[(shadowMap[i >> 3] & s_bitMask[i & 7]) >> s_bitShift[i & 7]];
            dst++;
            i += 2;
        }

        i++;
        *dst = s_shadowTexel[(shadowMap[i >> 3] & s_bitMask[i & 7]) >> s_bitShift[i & 7]];
        dst++;
        i++;

        if (y == size - 4) {
            i += size;
        }

        i += size;
    }
}

// VERIFY: 3cd77f91 manual
// OFFSET: 0x007B7C60
void __stdcall CMapRenderChunk::UnpackAlphaShadowBitsFixed8888Mip0(CImVector* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, int32_t alphaSlot, uint8_t* shadowMap) {
    CImVector* dst = dstBase + dstOffset;

    for (uint32_t y = 0; y < size; y++) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, cursors, size, alphaRows);

        for (uint32_t x = 0; x < size; x++) {
            CImVector color;

            if (alphaSlot) {
                int32_t alpha[4];
                alpha[1] = alphaRows[1][x];
                alpha[2] = alphaRows[2][x];
                alpha[3] = alphaRows[3][x];
                alpha[alphaSlot] = 255 - alpha[3] - alpha[2] - alpha[1] - alphaRows[0][x];
                color.b = alpha[3];
                color.g = alpha[2];
                color.r = alpha[1];
            } else {
                color.b = alphaRows[3][x];
                color.g = alphaRows[2][x];
                color.r = alphaRows[1][x];
            }

            color.a = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
            dst[x] = color;
        }

        dst += dstPitch;
    }
}

// VERIFY: 57dc0019 manual
// OFFSET: 0x007B7DC0
void __stdcall CMapRenderChunk::UnpackAlphaShadowBitsFixed8888Mip1(CImVector* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, int32_t alphaSlot, uint8_t* shadowMap) {
    CImVector* dst = dstBase + dstOffset;

    for (uint32_t y = 0; y < size; y += 2) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, cursors, size, alphaRows);

        uint32_t x = 0;
        uint32_t n = 0;

        for (; x < size - 2; x += 2) {
            CImVector color;

            if (alphaSlot) {
                int32_t alpha[4];
                alpha[1] = alphaRows[1][x];
                alpha[2] = alphaRows[2][x];
                alpha[3] = alphaRows[3][x];
                alpha[alphaSlot] = 255 - alpha[3] - alpha[2] - alpha[1] - alphaRows[0][x];
                color.b = alpha[3];
                color.g = alpha[2];
                color.r = alpha[1];
            } else {
                color.b = alphaRows[3][x];
                color.g = alphaRows[2][x];
                color.r = alphaRows[1][x];
            }

            color.a = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
            dst[n] = color;
            n++;
        }

        x++;

        CImVector color;

        if (alphaSlot) {
            int32_t alpha[4];
            alpha[1] = alphaRows[1][x];
            alpha[2] = alphaRows[2][x];
            alpha[3] = alphaRows[3][x];
            alpha[alphaSlot] = 255 - alpha[3] - alpha[2] - alpha[1] - alphaRows[0][x];
            color.b = alpha[3];
            color.g = alpha[2];
            color.r = alpha[1];
        } else {
            color.b = alphaRows[3][x];
            color.g = alphaRows[2][x];
            color.r = alphaRows[1][x];
        }

        color.a = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
        dst[n] = color;

        if (y < size - 2) {
            CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, cursors, size, alphaRows);
        }

        if (y == size - 4) {
            CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, cursors, size, alphaRows);
        }

        dst += dstPitch;
    }
}

// VERIFY: ddfa67aa manual
// OFFSET: 0x007B8070
void __stdcall CMapRenderChunk::UnpackAlphaShadowBitsFixed4444Mip0(uint16_t* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, uint8_t* shadowMap) {
    uint16_t* dst = dstBase + dstOffset;

    for (uint32_t y = 0; y < size; y++) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);

        for (uint32_t x = 0; x < size; x++) {
            uint32_t mask = s_nibbleMask[x & 1];
            uint32_t shift = s_nibbleShift[x & 1];
            uint8_t alpha1 = static_cast<uint8_t>((alphaRows[1][x >> 1] & mask) << shift);
            uint8_t alpha2 = static_cast<uint8_t>((alphaRows[2][x >> 1] & mask) << shift);
            uint8_t alpha3 = static_cast<uint8_t>((alphaRows[3][x >> 1] & mask) << shift);
            uint8_t shadow = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
            dst[x] = static_cast<uint16_t>(((shadow & 0xF0) << 8) | ((alpha1 & 0xF0) << 4) | (alpha2 & 0xF0) | (alpha3 >> 4));
        }

        dst += dstPitch;
    }
}

// VERIFY: 17932350 manual
// OFFSET: 0x007B8190
void __stdcall CMapRenderChunk::UnpackAlphaShadowBitsFixed4444Mip1(uint16_t* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, uint8_t* shadowMap) {
    uint16_t* dst = dstBase + dstOffset;

    for (uint32_t y = 0; y < size; y += 2) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);

        uint32_t x = 0;
        uint32_t n = 0;

        for (; x < size - 2; x += 2) {
            uint32_t mask = s_nibbleMask[x & 1];
            uint32_t shift = s_nibbleShift[x & 1];
            uint8_t alpha1 = static_cast<uint8_t>((alphaRows[1][x >> 1] & mask) << shift);
            uint8_t alpha2 = static_cast<uint8_t>((alphaRows[2][x >> 1] & mask) << shift);
            uint8_t alpha3 = static_cast<uint8_t>((alphaRows[3][x >> 1] & mask) << shift);
            uint8_t shadow = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
            dst[n] = static_cast<uint16_t>(((shadow & 0xF0) << 8) | ((alpha1 & 0xF0) << 4) | (alpha2 & 0xF0) | (alpha3 >> 4));
            n++;
        }

        x++;

        uint32_t mask = s_nibbleMask[x & 1];
        uint32_t shift = s_nibbleShift[x & 1];
        uint8_t alpha1 = static_cast<uint8_t>((alphaRows[1][x >> 1] & mask) << shift);
        uint8_t alpha2 = static_cast<uint8_t>((alphaRows[2][x >> 1] & mask) << shift);
        uint8_t alpha3 = static_cast<uint8_t>((alphaRows[3][x >> 1] & mask) << shift);
        uint8_t shadow = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
        dst[n] = static_cast<uint16_t>(((shadow & 0xF0) << 8) | ((alpha1 & 0xF0) << 4) | (alpha2 & 0xF0) | (alpha3 >> 4));

        CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);

        if (y == size - 4) {
            CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);
        }

        dst += dstPitch;
    }
}

// VERIFY: 3b181805 manual
// OFFSET: 0x007B84A0
void CMapRenderChunk::PackAlphaShadow4444(uint16_t* dst, uint8_t** alphaRows, uint8_t* shadowRow, uint32_t size) {
    uint16_t texel = 0;
    uint32_t x = 0;

    for (; x < size - 1; x++) {
        uint32_t mask = s_nibbleMask[x & 1];
        uint32_t shift = s_nibbleShift[x & 1];
        uint8_t shadow = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
        uint8_t alpha1 = static_cast<uint8_t>((alphaRows[1][x >> 1] & mask) << shift);
        uint8_t alpha2 = static_cast<uint8_t>((alphaRows[2][x >> 1] & mask) << shift);
        uint8_t alpha3 = static_cast<uint8_t>((alphaRows[3][x >> 1] & mask) << shift);
        texel = static_cast<uint16_t>(((shadow & 0xF0) << 8) | ((alpha1 & 0xF0) << 4) | (alpha2 & 0xF0) | (alpha3 >> 4));
        dst[x] = texel;
    }

    dst[x] = texel;
}

// VERIFY: d74fae7b manual
// OFFSET: 0x007B85A0
void __stdcall CMapRenderChunk::UnpackAlphaShadowBitsUnfixed4444Mip0(uint16_t* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, uint8_t* shadowMap) {
    uint16_t* dst = dstBase + dstOffset;
    uint8_t* shadowRow = nullptr;
    uint8_t* alphaRows[4];

    for (uint32_t y = 0; y < size - 1; y++) {
        CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);
        CMapRenderChunk::PackAlphaShadow4444(dst, alphaRows, shadowRow, size);
        dst += dstPitch;
    }

    CMapRenderChunk::PackAlphaShadow4444(dst, alphaRows, shadowRow, size);
}

// VERIFY: 81a839dc manual
// OFFSET: 0x007B8620
void __stdcall CMapRenderChunk::UnpackAlphaShadowBitsUnfixed4444Mip1(uint16_t* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, uint8_t* shadowMap) {
    uint16_t* dst = dstBase + dstOffset;

    for (uint32_t y = 0; y < size - 1; y += 2) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);

        uint16_t* texel = dst;

        for (uint32_t x = 0; x < size - 1; x += 2) {
            uint32_t mask = s_nibbleMask[x & 1];
            uint32_t shift = s_nibbleShift[x & 1];
            uint8_t alpha1 = static_cast<uint8_t>((alphaRows[1][x >> 1] & mask) << shift);
            uint8_t alpha2 = static_cast<uint8_t>((alphaRows[2][x >> 1] & mask) << shift);
            uint8_t alpha3 = static_cast<uint8_t>((alphaRows[3][x >> 1] & mask) << shift);
            uint8_t shadow = s_shadowAlpha[(shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]];
            *texel = static_cast<uint16_t>(((shadow & 0xF0) << 8) | ((alpha1 & 0xF0) << 4) | (alpha2 & 0xF0) | (alpha3 >> 4));
            texel++;
        }

        CMapRenderChunk::SetupAlphaRows4444(size, &shadowRow, alphaRows, cursors, &shadowMap);

        dst += dstPitch;
    }
}

// VERIFY: 0b452c5b manual
// OFFSET: 0x007B87F0
void CMapRenderChunk::UnpackAlphaShadowBits(void* dstBase, int32_t dstOffset, int32_t dstPitch, uint32_t size, SLayerAlpha* cursors, int32_t alphaSlot, uint8_t* shadowMap, int32_t genFormat, int32_t doNotFixAlphaMap) {
    if (doNotFixAlphaMap) {
        if (genFormat == 3) {
            if (this->m_renderFlags & 0x8) {
                CMapRenderChunk::UnpackAlphaShadowBitsFixed4444Mip1(static_cast<uint16_t*>(dstBase), dstOffset, dstPitch, size, cursors, shadowMap);
            } else {
                CMapRenderChunk::UnpackAlphaShadowBitsFixed4444Mip0(static_cast<uint16_t*>(dstBase), dstOffset, dstPitch, size, cursors, shadowMap);
            }
        } else {
            if (this->m_renderFlags & 0x8) {
                CMapRenderChunk::UnpackAlphaShadowBitsFixed8888Mip1(static_cast<CImVector*>(dstBase), dstOffset, dstPitch, size, cursors, alphaSlot, shadowMap);
            } else {
                CMapRenderChunk::UnpackAlphaShadowBitsFixed8888Mip0(static_cast<CImVector*>(dstBase), dstOffset, dstPitch, size, cursors, alphaSlot, shadowMap);
            }
        }

        return;
    }

    if (genFormat != 3) {
        SErrDisplayAppFatal("CMapChunk::UnpackAlphaShadowBits(): Bad genformat.");
    }

    if (this->m_renderFlags & 0x8) {
        CMapRenderChunk::UnpackAlphaShadowBitsUnfixed4444Mip1(static_cast<uint16_t*>(dstBase), dstOffset, dstPitch, size, cursors, shadowMap);
    } else {
        CMapRenderChunk::UnpackAlphaShadowBitsUnfixed4444Mip0(static_cast<uint16_t*>(dstBase), dstOffset, dstPitch, size, cursors, shadowMap);
    }
}

// VERIFY: b610665e manual
// OFFSET: 0x007B88D0
void __stdcall CMapRenderChunk::RecreateAlphaBitsFixed8888Mip0(CImVector* dst, uint32_t size, SLayerAlpha* layer, uint8_t* shadowMap) {
    for (uint32_t y = 0; y < size; y++) {
        uint8_t* shadowRow;

        if (shadowMap) {
            shadowRow = shadowMap;
            shadowMap += size >> 3;
        } else {
            shadowRow = s_defaultShadowRow;
        }

        uint8_t* alphaRow;

        if (layer->m_flags & 0x200) {
            alphaRow = s_alphaRowBuf[0];
            layer->m_alphaData = CMapRenderChunk::DecodeAlphaRLE(layer->m_alphaData, s_alphaRowBuf[0], size);
        } else {
            alphaRow = layer->m_alphaData;
            layer->m_alphaData += size;
        }

        for (uint32_t x = 0; x < size; x++) {
            uint32_t alpha = alphaRow[x];

            if ((shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]) {
                alpha = (178 * alpha) >> 8;
            }

            CImVector color;
            color.b = alpha;
            color.g = alpha;
            color.r = alpha;
            color.a = alpha;
            *dst = color;
            dst++;
        }
    }
}

// VERIFY: 7b569ca8 manual
// OFFSET: 0x007B89C0
void __stdcall CMapRenderChunk::RecreateAlphaBitsFixed8888Mip1(CImVector* dst, uint32_t size, SLayerAlpha* layer, uint8_t* shadowMap) {
    for (uint32_t y = 0; y < size; y += 2) {
        uint8_t* shadowRow;

        if (shadowMap) {
            shadowRow = shadowMap;
            shadowMap += size >> 3;
        } else {
            shadowRow = s_defaultShadowRow;
        }

        uint8_t* alphaRow;

        if (layer->m_flags & 0x200) {
            alphaRow = s_alphaRowBuf[0];
            layer->m_alphaData = CMapRenderChunk::DecodeAlphaRLE(layer->m_alphaData, s_alphaRowBuf[0], size);
        } else {
            alphaRow = layer->m_alphaData;
            layer->m_alphaData += size;
        }

        uint32_t x = 0;

        for (; x < size - 2; x += 2) {
            uint32_t alpha = alphaRow[x];

            if ((shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]) {
                alpha = (178 * alpha) >> 8;
            }

            CImVector color;
            color.b = alpha;
            color.g = alpha;
            color.r = alpha;
            color.a = alpha;
            *dst = color;
            dst++;
        }

        x++;

        uint32_t alpha = alphaRow[x];

        if ((shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]) {
            alpha = (178 * alpha) >> 8;
        }

        CImVector color;
        color.b = alpha;
        color.g = alpha;
        color.r = alpha;
        color.a = alpha;
        *dst = color;
        dst++;

        if (y < size - 2) {
            if (shadowMap) {
                shadowMap += size >> 3;
            }

            if (layer->m_flags & 0x200) {
                layer->m_alphaData = CMapRenderChunk::DecodeAlphaRLE(layer->m_alphaData, s_alphaRowBuf[0], size);
            } else {
                layer->m_alphaData += size;
            }
        }

        if (y == size - 4) {
            if (shadowMap) {
                shadowMap += size >> 3;
            }

            if (layer->m_flags & 0x200) {
                layer->m_alphaData = CMapRenderChunk::DecodeAlphaRLE(layer->m_alphaData, s_alphaRowBuf[0], size);
            } else {
                layer->m_alphaData += size;
            }
        }
    }
}

// VERIFY: 24f3ebfc manual
// OFFSET: 0x007B8B80
void CMapRenderChunk::UnpackAlphaBitsFixed8888Mip0(CImVector* dst, uint32_t size, uint8_t* shadowMap) {
    SLayerAlpha layers[4];
    this->CollectLayerAlphaPtrs(this->m_mapChunks[0], layers);

    for (uint32_t y = 0; y < size; y++) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, layers, size, alphaRows);

        for (uint32_t x = 0; x < size; x++) {
            uint32_t alpha = 255 - alphaRows[1][x] - alphaRows[3][x] - alphaRows[2][x];

            if ((shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]) {
                alpha = (178 * alpha) >> 8;
            }

            CImVector color;
            color.b = alpha;
            color.g = alpha;
            color.r = alpha;
            color.a = alpha;
            *dst = color;
            dst++;
        }
    }
}

// VERIFY: d75a2a5a manual
// OFFSET: 0x007B8C70
void CMapRenderChunk::UnpackAlphaBitsFixed8888Mip1(CImVector* dst, uint32_t size, uint8_t* shadowMap) {
    SLayerAlpha layers[4];
    this->CollectLayerAlphaPtrs(this->m_mapChunks[0], layers);

    for (uint32_t y = 0; y < size; y += 2) {
        uint8_t* shadowRow;
        uint8_t* alphaRows[4];
        CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, layers, size, alphaRows);

        uint32_t x = 0;

        for (; x < size - 2; x += 2) {
            uint32_t alpha = 255 - alphaRows[1][x] - alphaRows[3][x] - alphaRows[2][x];

            if ((shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]) {
                alpha = (178 * alpha) >> 8;
            }

            CImVector color;
            color.b = alpha;
            color.g = alpha;
            color.r = alpha;
            color.a = alpha;
            *dst = color;
            dst++;
        }

        x++;

        uint32_t alpha = 255 - alphaRows[3][x] - alphaRows[2][x] - alphaRows[1][x];

        if ((shadowRow[x >> 3] & s_bitMask[x & 7]) >> s_bitShift[x & 7]) {
            alpha = (178 * alpha) >> 8;
        }

        CImVector color;
        color.b = alpha;
        color.g = alpha;
        color.r = alpha;
        color.a = alpha;
        *dst = color;
        dst++;

        if (y < size - 2) {
            CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, layers, size, alphaRows);
        }

        if (y == size - 4) {
            CMapRenderChunk::SetupAlphaRows(&shadowMap, &shadowRow, layers, size, alphaRows);
        }
    }
}

// VERIFY: 4cab62df manual
// OFFSET: 0x007B8E20
void CMapRenderChunk::UnpackAlphaBits(void* dst, uint32_t size, SLayerAlpha* layerInfo, uint8_t* shadowMap, int32_t layerMode, int32_t bigAlpha) {
    if (bigAlpha) {
        if (layerMode == 3) {
            if (this->m_renderFlags & 0x8) {
                CMapRenderChunk::UnpackAlphaBitsFixed4444Mip1(static_cast<uint16_t*>(dst), size, layerInfo);
            } else {
                CMapRenderChunk::UnpackAlphaBitsFixed4444Mip0(static_cast<uint16_t*>(dst), size, layerInfo);
            }

            return;
        }

        if (layerMode == 2) {
            if (layerInfo->m_alphaData) {
                if (this->m_renderFlags & 0x8) {
                    CMapRenderChunk::RecreateAlphaBitsFixed8888Mip1(static_cast<CImVector*>(dst), size, layerInfo, shadowMap);
                } else {
                    CMapRenderChunk::RecreateAlphaBitsFixed8888Mip0(static_cast<CImVector*>(dst), size, layerInfo, shadowMap);
                }
            } else {
                if (this->m_renderFlags & 0x8) {
                    this->UnpackAlphaBitsFixed8888Mip1(static_cast<CImVector*>(dst), size, shadowMap);
                } else {
                    this->UnpackAlphaBitsFixed8888Mip0(static_cast<CImVector*>(dst), size, shadowMap);
                }
            }

            return;
        }
    } else if (layerMode == 3) {
        if (this->m_renderFlags & 0x8) {
            CMapRenderChunk::UnpackAlphaBitsUnfixed4444Mip1(static_cast<uint16_t*>(dst), size, layerInfo);
        } else {
            CMapRenderChunk::UnpackAlphaBitsUnfixed4444Mip0(static_cast<uint16_t*>(dst), size, layerInfo);
        }

        return;
    }

    SErrDisplayAppFatal("CMapChunk::UnpackAlphaBits(): Bad genformat.");
}

// VERIFY: 97b6c842 manual
// OFFSET: 0x007B9890
void CMapRenderChunk::CreateChunkLayerTex(CMapRenderChunkLayer* layer) {
    int32_t genFormat = 3;

    if (CMap::header.m_flags & 0x4) {
        genFormat = 2;
    }

    CMapChunk* chunk = this->m_mapChunks[0];
    SMLayer* chunkLayer = &chunk->m_layers[static_cast<uint16_t>(layer->m_layerIndex)];
    uint32_t layerFlags = chunkLayer->m_flags;
    uint8_t* alphaData = nullptr;

    if (layerFlags & 0x100) {
        alphaData = &chunk->m_additionalShadowmap[chunkLayer->m_offsetInMCAL];
    }

    uint32_t chunkFlags = chunk->m_header->m_flags;
    uint8_t* shadowMap = nullptr;

    if ((chunkFlags & 0x1) && (CWorld::enables & Enable_Shadow)) {
        shadowMap = chunk->m_shadowMap;
    }

    SLayerAlpha layerAlpha;
    layerAlpha.m_flags = layerFlags;
    layerAlpha.m_alphaData = alphaData;

    CMapArea* area = static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref);
    this->UnpackAlphaBits(CMapRenderChunk::s_defaultTex, 64 >> area->m_header->m_mampValue, &layerAlpha, shadowMap, genFormat, chunkFlags & 0x8000);
}

// VERIFY: ffeb78ca manual
// OFFSET: 0x007B9950
void CMapRenderChunk::CreateShadowTex() {
    CMapChunk* chunk = this->m_mapChunks[0];
    uint8_t* shadowMap = nullptr;

    if (chunk->m_header->m_flags & 0x1) {
        shadowMap = chunk->m_shadowMap;
    }

    CMapArea* area = static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref);
    uint32_t size = 64 >> area->m_header->m_mampValue;

    if (this->m_renderFlags & 0x8) {
        CMapRenderChunk::ExpandShadowBitsToTexelsMip(reinterpret_cast<uint16_t*>(CMapRenderChunk::s_defaultTex), size, shadowMap);
    } else {
        CMapRenderChunk::ExpandShadowBitsToTexels(reinterpret_cast<uint16_t*>(CMapRenderChunk::s_defaultTex), size, shadowMap);
    }
}

// VERIFY: f4962761 manual
// OFFSET: 0x007B99B0
void CMapRenderChunk::CreateShaderTexture() {
    int32_t genFormat = 3;

    if (CMap::header.m_flags & 0x4) {
        genFormat = 2;
    }

    CMapChunk* chunk = this->m_mapChunks[0];
    uint32_t layerCount = chunk->m_header->m_nLayers;
    uint8_t* shadowMap = nullptr;
    SLayerAlpha layers[4];
    layers[0].m_alphaData = nullptr;
    layers[1].m_alphaData = nullptr;
    layers[2].m_alphaData = nullptr;
    layers[3].m_alphaData = nullptr;

    for (uint32_t i = 0; i < layerCount; i++) {
        uint32_t layerFlags = chunk->m_layers[i].m_flags;

        if (layerFlags & 0x100) {
            layers[i].m_alphaData = &chunk->m_additionalShadowmap[chunk->m_layers[i].m_offsetInMCAL];
        }

        layers[i].m_flags = layerFlags;
    }

    uint32_t chunkFlags = chunk->m_header->m_flags;

    if ((chunkFlags & 0x1) && (CWorld::enables & Enable_Shadow)) {
        shadowMap = chunk->m_shadowMap;
    }

    CMapArea* area = static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref);
    int32_t size = 64 >> area->m_header->m_mampValue;
    int32_t pitch = size;

    if (this->m_unkFlags & 0x2) {
        pitch = size * 2;
    }

    if (this->m_renderFlags & 0x8) {
        pitch >>= 1;
    }

    this->UnpackAlphaShadowBits(CMapRenderChunk::s_defaultTex, 0, pitch, size, layers, 0, shadowMap, genFormat, chunkFlags & 0x8000);

    if (!this->m_mapChunks[1]) {
        return;
    }

    SMChunk* header = this->m_mapChunks[1]->m_header;
    int32_t alphaSlot = 0;
    layers[0].m_alphaData = nullptr;
    layers[1].m_alphaData = nullptr;
    layers[2].m_alphaData = nullptr;
    layers[3].m_alphaData = nullptr;
    uint32_t chunkLayerCount = header->m_nLayers;

    for (uint32_t j = 0; j < chunkLayerCount; j++) {
        SMLayer* chunkLayer = &this->m_mapChunks[1]->m_layers[j];

        for (uint32_t i = 0; i < this->m_layersCount; i++) {
            if (this->m_layers[i].m_textureId != chunkLayer->m_textureId) {
                continue;
            }

            uint32_t layerFlags = chunkLayer->m_flags;

            if (layerFlags & 0x100) {
                layers[i].m_alphaData = &this->m_mapChunks[1]->m_additionalShadowmap[chunkLayer->m_offsetInMCAL];
            }

            layers[i].m_flags = layerFlags;

            if (j == 0) {
                alphaSlot = i;
            }

            break;
        }
    }

    uint32_t chunk2Flags = header->m_flags;
    uint8_t* shadowMap2 = nullptr;

    if ((chunk2Flags & 0x1) && (CWorld::enables & Enable_Shadow)) {
        shadowMap2 = this->m_mapChunks[1]->m_shadowMap;
    }

    int32_t offset = 0;

    if (this->m_unkFlags & 0x1) {
        offset = size * size;

        if (this->m_renderFlags & 0x8) {
            offset >>= 2;
        }
    } else if (this->m_unkFlags & 0x2) {
        offset = size;

        if (this->m_renderFlags & 0x8) {
            offset = size >> 1;
        }
    }

    this->UnpackAlphaShadowBits(CMapRenderChunk::s_defaultTex, offset, pitch, size, layers, alphaSlot, shadowMap2, genFormat, chunk2Flags & 0x8000);
}

// VERIFY: ee6e5438 manual
// OFFSET: 0x007B9BC0
void CMapRenderChunk::UpdateLayerGxTexture(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (cmd != GxTex_Latch) {
        return;
    }

    CMapRenderChunkLayer* layer = static_cast<CMapRenderChunkLayer*>(userArg);
    layer->m_owner->CreateChunkLayerTex(layer);

    uint32_t stride = 4 * w;

    if (!(CMap::header.m_flags & 0x4)) {
        stride = 2 * w;
    }

    texelStrideInBytes = stride;
    texels = CMapRenderChunk::s_defaultTex;
}

// VERIFY: ea80bed7 manual
// OFFSET: 0x007B9C20
void CMapRenderChunk::UpdateShadowGxTexture(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (cmd != GxTex_Latch) {
        return;
    }

    static_cast<CMapRenderChunk*>(userArg)->CreateShadowTex();
    texelStrideInBytes = 2 * w;
    texels = CMapRenderChunk::s_defaultTex;
}

// VERIFY: 03ca5953 manual
// OFFSET: 0x007B9C60
void CMapRenderChunk::UpdateShaderGxTexture(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    if (cmd != GxTex_Latch) {
        return;
    }

    static_cast<CMapRenderChunk*>(userArg)->CreateShaderTexture();
    texels = CMapRenderChunk::s_defaultTex;

    uint32_t stride = 4 * w;

    if (!(CMap::header.m_flags & 0x4)) {
        stride = 2 * w;
    }

    texelStrideInBytes = stride;
}

// VERIFY: db4ab7ce manual
// OFFSET: 0x007B9DE0
void CMapRenderChunk::AllocLayerTexture(CMapRenderChunkLayer* layer) {
    if (layer->m_layerTexture) {
        HandleClose(reinterpret_cast<HOBJECT>(layer->m_layerTexture));
    }

    layer->m_layerTexture = nullptr;

    uint32_t bigAlpha = (CMap::header.m_flags >> 2) & 1;
    int32_t baseLayer = bigAlpha && layer->m_layerIndex == 0;

    if (!(layer->m_flags & 0x100) && !baseLayer) {
        return;
    }

    CMapChunk* chunk = this->m_mapChunks[0];
    int32_t width = 64 >> static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref)->m_header->m_mampValue;
    int32_t height = 64 >> static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref)->m_header->m_mampValue;

    if (this->m_renderFlags & 0x8) {
        width /= 2;
        height /= 2;
    }

    CTexture* texture;

    if (bigAlpha) {
        texture = CMapRenderChunk::AllocTexture(width, height, layer, CMapRenderChunk::UpdateLayerGxTexture, GxTex_Argb8888, GxTex_Argb8888);
    } else {
        texture = CMapRenderChunk::AllocTexture(width, height, layer, CMapRenderChunk::UpdateLayerGxTexture, GxTex_Argb4444, GxTex_Argb4444);
    }

    layer->m_layerTexture = texture;
    GxTexUpdate(TextureGetGxTex(texture, 1, nullptr), 0, 0, width, height, 1);
}

// VERIFY: 20e9adae auto
// OFFSET: 0x007B9EE0
void CMapRenderChunk::AllocShadowTexture() {
    if (this->m_shadowTexture) {
        HandleClose(reinterpret_cast<HOBJECT>(this->m_shadowTexture));
    }

    this->m_shadowTexture = nullptr;

    if (!(CWorld::enables & Enable_Shadow)) {
        return;
    }

    CMapChunk* chunk = this->m_mapChunks[0];

    if (!(chunk->m_header->m_flags & 0x1)) {
        return;
    }

    int32_t width = 64 >> static_cast<CMapArea*>(chunk->m_parentLinkList.Head()->m_ref)->m_header->m_mampValue;
    int32_t height = width;

    if (this->m_renderFlags & 0x8) {
        width /= 2;
        height = width;
    }

    CTexture* texture = CMapRenderChunk::AllocTexture(width, height, this, CMapRenderChunk::UpdateShadowGxTexture, GxTex_Argb4444, GxTex_Argb4444);
    this->m_shadowTexture = texture;
    GxTexUpdate(TextureGetGxTex(texture, 1, nullptr), 0, 0, width, height, 1);
}

// VERIFY: fad6a25c manual
// OFFSET: 0x007B9F90
void CMapRenderChunk::AllocShaderTexture() {
    if (this->m_terrainBlendTexture) {
        HandleClose(reinterpret_cast<HOBJECT>(this->m_terrainBlendTexture));
    }

    this->m_terrainBlendTexture = nullptr;

    int32_t width = 64 >> static_cast<CMapArea*>(this->m_mapChunks[0]->m_parentLinkList.Head()->m_ref)->m_header->m_mampValue;
    int32_t height = width;

    if (this->m_renderFlags & 0x8) {
        width /= 2;
        height = width;
    }

    if (this->m_unkFlags & 0x1) {
        height *= 2;
    } else if (this->m_unkFlags & 0x2) {
        width *= 2;
    }

    CTexture* texture;

    if (CMap::header.m_flags & 0x4) {
        texture = CMapRenderChunk::AllocTexture(width, height, this, CMapRenderChunk::UpdateShaderGxTexture, GxTex_Argb8888, GxTex_Argb8888);
    } else {
        texture = CMapRenderChunk::AllocTexture(width, height, this, CMapRenderChunk::UpdateShaderGxTexture, GxTex_Argb4444, GxTex_Argb4444);
    }

    this->m_terrainBlendTexture = texture;
    GxTexUpdate(TextureGetGxTex(texture, 1, nullptr), 0, 0, width, height, 1);
}

// VERIFY: 913e0d32 manual
// OFFSET: 0x007BA050
void CMapRenderChunk::AllocLayerTextures() {
    if (this->m_layersCount) {
        int16_t flags = this->m_renderFlags;
        int32_t wantMip = (flags & 0x10) != 0;
        int32_t haveMip = (flags & 0x8) != 0;

        if ((flags & 0x20) || wantMip != haveMip) {
            this->m_renderFlags = flags & ~0x8;

            if (this->m_renderFlags & 0x10) {
                this->m_renderFlags |= 0x8;
            }

            if (CMap::gTerrainPixelShadersValid || CMap::enableSpecularTerrain) {
                this->AllocShaderTexture();
            } else {
                for (uint32_t i = 0; i < this->m_layersCount; i++) {
                    this->AllocLayerTexture(&this->m_layers[i]);
                }

                if (!(CMap::header.m_flags & 0x4)) {
                    this->AllocShadowTexture();
                }
            }
        }
    }

    this->m_renderFlags &= ~0x30;
}

// VERIFY: 7488cd14 manual
// OFFSET: 0x0079E470
CGxShader* CMapRenderChunk::GetVertexShader(int32_t pointLights, int32_t layers, int32_t specular, int32_t vertexColor, int32_t a5, int32_t shadow) {
    return CMap::vertexShader_Terrain[64 * pointLights + 16 * (layers - 1) + 8 * specular + 4 * vertexColor + 2 * a5 + (shadow != 0)];
}

// VERIFY: a33ed04d auto
// OFFSET: 0x007CFBE0
void CMapRenderChunk::InitializeVertexShaderConstants(const C44Matrix& world, const C44Matrix& view) {
    memset(&CMapRenderChunk::s_vertexShaderConsts, 0, sizeof(CMapRenderChunk::s_vertexShaderConsts));

    SVertexShaderConstants& consts = CMapRenderChunk::s_vertexShaderConsts;
    consts.m_worldView = world * view;
    consts.m_viewTranspose = view.Transpose();

    CGxDevice* device = g_theGxDevicePtr;
    consts.m_projection = device->m_projNative;

    if (device->DeviceApi() == GxApi_OpenGl) {
        consts.m_projection.c0 *= -1.0f;
        consts.m_projection.c1 *= -1.0f;
        consts.m_projection.c2 *= -1.0f;
        consts.m_projection.c3 *= -1.0f;
    }

    consts.m_texOffset[0].x = 0.0f;
    consts.m_texOffset[0].y = 0.0f;
    consts.m_texOffset[0].z = 0.0f;
    consts.m_texOffset[0].w = 0.0f;
    consts.m_texOffset[1].x = 0.0f;
    consts.m_texOffset[1].y = 0.0f;
    consts.m_texOffset[1].z = 0.0f;
    consts.m_texOffset[1].w = 0.0f;
    consts.m_texOffset[2].x = 0.0f;
    consts.m_texOffset[2].y = 0.0f;
    consts.m_texOffset[2].z = 0.0f;
    consts.m_texOffset[2].w = 0.0f;
    consts.m_texOffset[3].x = 0.0f;
    consts.m_texOffset[3].y = 0.0f;
    consts.m_texOffset[3].z = 0.0f;
    consts.m_texOffset[3].w = 0.0f;
    consts.m_texScale[0].x = 0.0f;
    consts.m_texScale[0].y = 0.0f;
    consts.m_texScale[0].z = 0.0f;
    consts.m_texScale[0].w = 0.0f;

    C3Vector sunDir(0.0f, 0.0f, 0.0f);
    CM2Lighting lighting(sunDir);
    lighting.AddLight(&CMap::s_mapLight->m_light);

    if (lighting.GetSunLight(sunDir, consts.m_sunAmbient, consts.m_sunDiffuse, consts.m_sunSpecular)) {
        C33Matrix rotation(consts.m_worldView);
        C3Vector dir = -sunDir * rotation;
        consts.m_sunDir.x = dir.x;
        consts.m_sunDir.y = dir.y;
        consts.m_sunDir.z = dir.z;
        consts.m_sunDir.w = 1.0f;
        consts.m_sunSpecularW = 20.0f;
    } else {
        consts.m_sunAmbient = C3Vector(1.0f, 1.0f, 1.0f);
        consts.m_sunAmbientW = 1.0f;
        consts.m_sunSpecular = C3Vector(0.0f, 0.0f, 0.0f);
        consts.m_sunSpecularW = 0.0f;
    }

    for (int32_t i = 0; i < 3; i++) {
        SVertexShaderConstants::SPointLight& light = consts.m_pointLights[i];
        light.m_position = C3Vector(0.0f, 0.0f, 0.0f);
        light.m_positionW = 0.0f;
        light.m_color = C3Vector(0.0f, 0.0f, 0.0f);
        light.m_colorW = 0.0f;
        light.m_attenuation = C3Vector(1.0f, 0.0f, 0.0f);
        light.m_attenuationW = 0.0f;
    }

    if (g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)) {
        DayNight::DNInfo* info = DayNight::GetInfo();
        float end = info->m_fog.m_end;
        float range = 1.0f / (end - info->m_fog.m_start);
        consts.m_fog.x = -(CShaderEffect::s_fogMul * range);
        consts.m_fog.y = end * range;
        consts.m_fog.z = info->m_fog.m_density;
        consts.m_fog.w = 0.0f;
    } else {
        consts.m_fog.x = 0.0f;
        consts.m_fog.y = 1.0f;
        consts.m_fog.z = 1.0f;
        consts.m_fog.w = 0.0f;
    }
}

// VERIFY: aed99aca manual
// OFFSET: 0x007D0050
void CMapRenderChunk::SetVertexShader(CMapRenderChunk* renderChunk, int32_t layerCount, int32_t a3) {
    SVertexShaderConstants& consts = CMapRenderChunk::s_vertexShaderConsts;
    int32_t specular = CMap::enableSpecularTerrain;
    int32_t vertexColor = CMapRenderChunk::s_gxBufVertexFormat == GxVBF_PNC;

    CM2Lighting lighting(renderChunk->m_sphere);
    World::s_m2Scene->SelectLights(&lighting);
    renderChunk->SelectLights(&lighting);

    C3Vector position(0.0f, 0.0f, 0.0f);
    int32_t pointLights = 0;

    for (uint32_t i = 0; i < 3; i++) {
        SVertexShaderConstants::SPointLight& light = consts.m_pointLights[i];

        if (lighting.GetPointLight(i, position, light.m_color, light.m_attenuation)) {
            light.m_position = position - CWorldScene::s_activeWorldView;
            light.m_positionW = 1.0f;
            pointLights = 1;
        } else {
            light.m_position = C3Vector(0.0f, 0.0f, 0.0f);
            light.m_positionW = 0.0f;
            light.m_color = C3Vector(0.0f, 0.0f, 0.0f);
            light.m_colorW = 0.0f;
            light.m_attenuation = C3Vector(1.0f, 0.0f, 0.0f);
            light.m_attenuationW = 0.0f;
        }
    }

    consts.m_position.x = renderChunk->m_vec1.x;
    consts.m_position.y = renderChunk->m_vec1.y;
    consts.m_position.z = renderChunk->m_vec1.z;
    consts.m_position.w = 0.0f;

    C4Vector layerScale;
    layerScale.x = -CMapChunk::s_geoToTex;
    layerScale.y = -CMapChunk::s_geoToTex;
    layerScale.z = 0.0f;
    layerScale.w = 0.0f;

    for (int32_t i = 1; i < layerCount; i++) {
        consts.m_texScale[i] = layerScale;
    }

    C4Vector alphaScale;
    alphaScale.x = -CMapChunk::s_geoToTex * 0.125f;
    alphaScale.y = -CMapChunk::s_geoToTex * 0.125f;
    alphaScale.z = 0.0f;
    alphaScale.w = 0.0f;

    if (renderChunk->m_unkFlags & 0x1) {
        alphaScale.x = CMapChunk::s_geoToTex * -0.0625f;
    } else if (renderChunk->m_unkFlags & 0x2) {
        alphaScale.y = CMapChunk::s_geoToTex * -0.0625f;
    }

    consts.m_texScale[layerCount] = alphaScale;

    CGxShader* shader = CMapRenderChunk::GetVertexShader(pointLights, layerCount - 1, specular, vertexColor, a3, CShadowCache::GetShadowValue());
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, shader);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&consts), sizeof(SVertexShaderConstants) / sizeof(C4Vector));
}

// VERIFY: 2226742d manual
// OFFSET: 0x007D06B0
void CMapRenderChunk::ComputeTexMatrices(C44Matrix& layerMatrix, C44Matrix& alphaMatrix, const C3Vector& offset, float scale) {
    layerMatrix.Scale(scale);

    float a0 = layerMatrix.a0;
    float a1 = layerMatrix.a1;
    float a2 = layerMatrix.a2;
    float a3 = layerMatrix.a3;
    layerMatrix.a0 = layerMatrix.b0;
    layerMatrix.a1 = layerMatrix.b1;
    layerMatrix.a2 = layerMatrix.b2;
    layerMatrix.a3 = layerMatrix.b3;
    layerMatrix.b0 = a0;
    layerMatrix.b1 = a1;
    layerMatrix.b2 = a2;
    layerMatrix.b3 = a3;
    layerMatrix.Translate(offset);

    alphaMatrix.Scale(scale * 0.125f);

    a0 = alphaMatrix.a0;
    a1 = alphaMatrix.a1;
    a2 = alphaMatrix.a2;
    a3 = alphaMatrix.a3;
    alphaMatrix.a0 = alphaMatrix.b0;
    alphaMatrix.a1 = alphaMatrix.b1;
    alphaMatrix.a2 = alphaMatrix.b2;
    alphaMatrix.a3 = alphaMatrix.b3;
    alphaMatrix.b0 = a0;
    alphaMatrix.b1 = a1;
    alphaMatrix.b2 = a2;
    alphaMatrix.b3 = a3;
    alphaMatrix.Translate(offset);
}

// VERIFY: 0307c652 manual
// OFFSET: 0x007D0760
void CMapRenderChunk::RenderMultiPassAdditive(CMapRenderChunk* renderChunk) {
    C44Matrix layerMatrix;
    C44Matrix alphaMatrix;
    C3Vector offset = CWorldScene::s_activeWorldView - renderChunk->m_vec1;
    CMapRenderChunk::ComputeTexMatrices(layerMatrix, alphaMatrix, offset, -CMapChunk::s_geoToTex);
    GxXformSet(GxXform_Tex0, layerMatrix);
    GxXformSet(GxXform_Tex1, alphaMatrix);

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    int32_t specular = (renderChunk->m_renderFlags & 0x4) != 0;

    for (int32_t i = 0; i < renderChunk->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &renderChunk->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_ReflectionMap);
            GxRsSet(GxRs_TextureShader0, 0);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 0);
        }

        if (layer->m_flags & 0x40) {
            C3Vector move(World::texVect[layer->m_flags & 7]);
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            move.x *= moveScale;
            move.y *= moveScale;
            move.z *= moveScale;
            GxXformTranslate(GxXform_Tex0, move);
        }

        if (i == 1) {
            GxRsSet(GxRs_BlendingMode, GxBlend_NoAlphaAdd);
            GxUpdateAlphaRef(GxRs_AlphaRef);

            CImVector black;
            black.b = 0;
            black.g = 0;
            black.r = 0;
            black.a = 0;
            GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&black));
        }

        if (layer->m_layerTexture) {
            g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(layer->m_layerTexture, 1, nullptr));
        } else {
            GxRsSet(GxRs_Texture1, 0);
        }

        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_EyeLinear);
            GxRsSet(GxRs_TextureShader0, 1);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 1);
        }

        if (layer->m_flags & 0x40) {
            GxXformSet(GxXform_Tex0, layerMatrix);
        }
    }

    GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&CMapRenderChunk::s_fogColor));
    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

struct TerrainCapture {
    CMapRenderChunk* chunk;
    C44Matrix world;
    C44Matrix tex0;
    C44Matrix tex1;
    float lightPos[4][4];
    float lightDiffuse[4][4];
    float lightAmbient[4][4];
    float lightAtten[4][4];
    float fogStart;
    float fogEnd;
    uint32_t fogColor;
    int32_t fog;
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t baseVertex;
};

static std::vector<TerrainCapture> s_terrainBatch;

namespace {

struct TerrainGroupKey {
    CTexture* texture;
    CTexture* layerTexture;
    int32_t fog;
    uint32_t fogColor;
    float fogStart;
    float fogEnd;

    bool operator==(const TerrainGroupKey& other) const {
        return texture == other.texture && layerTexture == other.layerTexture && fog == other.fog && fogColor == other.fogColor && fogStart == other.fogStart && fogEnd == other.fogEnd;
    }
};

TerrainGroupKey MakeTerrainGroupKey(const TerrainCapture& cap, uint32_t layer, int32_t bindless) {
    const CMapRenderChunkLayer& chunkLayer = cap.chunk->m_layers[layer];
    TerrainGroupKey key;
    key.texture = bindless ? nullptr : chunkLayer.m_texture;
    key.layerTexture = bindless ? (chunkLayer.m_layerTexture ? reinterpret_cast<CTexture*>(1) : nullptr) : chunkLayer.m_layerTexture;
    key.fog = cap.fog;
    key.fogColor = cap.fogColor;
    key.fogStart = cap.fogStart;
    key.fogEnd = cap.fogEnd;
    return key;
}

std::vector<GxTerrainDraw> s_terrainDraws;
std::vector<TerrainGroupKey> s_terrainGroupKeys;
std::vector<std::vector<size_t>> s_terrainGroupMembers;
std::vector<uint8_t> s_meshTaken;

}

static void CaptureLights(TerrainCapture& cap) {
    memset(cap.lightPos, 0, sizeof(cap.lightPos));
    memset(cap.lightDiffuse, 0, sizeof(cap.lightDiffuse));
    memset(cap.lightAmbient, 0, sizeof(cap.lightAmbient));
    memset(cap.lightAtten, 0, sizeof(cap.lightAtten));

    for (uint32_t i = 0; i < 4; ++i) {
        const CGxApiLight& light = g_theGxDevicePtr->m_lights[i];

        if (!light.m_enable) {
            continue;
        }

        cap.lightPos[i][0] = light.m_dir.x;
        cap.lightPos[i][1] = light.m_dir.y;
        cap.lightPos[i][2] = light.m_dir.z;
        cap.lightPos[i][3] = light.m_dir.w == 1.0f ? 1.0f : 0.0f;
        cap.lightDiffuse[i][0] = light.m_dirColor.x;
        cap.lightDiffuse[i][1] = light.m_dirColor.y;
        cap.lightDiffuse[i][2] = light.m_dirColor.z;
        cap.lightDiffuse[i][3] = 1.0f;
        cap.lightAmbient[i][0] = light.m_ambColor.x;
        cap.lightAmbient[i][1] = light.m_ambColor.y;
        cap.lightAmbient[i][2] = light.m_ambColor.z;
        cap.lightAtten[i][0] = light.m_constantAttenuation;
        cap.lightAtten[i][1] = light.m_linearAttenuation;
        cap.lightAtten[i][2] = light.m_quadraticAttenuation;
    }
}

static void ApplyChunkFog(const TerrainCapture& cap) {
    GxRsSet(GxRs_FogStart, cap.fogStart);
    GxRsSet(GxRs_FogEnd, cap.fogEnd);
    GxRsSet(GxRs_FogColor, static_cast<int32_t>(cap.fogColor));
    GxRsSet(GxRs_Fog, cap.fog);
}

static void FillTerrainDraw(GxTerrainDraw& draw, const TerrainCapture& cap) {
    draw.world = cap.world;
    GxXformSet(GxXform_Tex0, cap.tex0);
    GxXformSet(GxXform_Tex1, cap.tex1);
    // ʕ ◕ᴥ◕ ʔ✿ Eye-linear texgen is the view inverse, refreshed by IRsSync before this ✿ ʕ ◕ᴥ◕ ʔ
    g_theGxDevicePtr->ResolveTexMatrix(0, draw.tex0);
    g_theGxDevicePtr->ResolveTexMatrix(1, draw.tex1);
    memcpy(draw.lightPos, cap.lightPos, sizeof(draw.lightPos));
    memcpy(draw.lightDiffuse, cap.lightDiffuse, sizeof(draw.lightDiffuse));
    memcpy(draw.lightAmbient, cap.lightAmbient, sizeof(draw.lightAmbient));
    memcpy(draw.lightAtten, cap.lightAtten, sizeof(draw.lightAtten));
    draw.indexCount = cap.indexCount;
    draw.firstIndex = cap.firstIndex;
    draw.baseVertex = cap.baseVertex;
}

static void DrawTerrainChunk(const TerrainCapture& cap) {
    ApplyChunkFog(cap);
    GxXformSet(GxXform_World, cap.world);
    GxXformSet(GxXform_Tex0, cap.tex0);
    GxXformSet(GxXform_Tex1, cap.tex1);
    GxPrimVertexPtr(cap.chunk->m_chunkBuf->m_vertexBuf, CMapRenderChunk::s_gxBufVertexFormat);
    g_theGxDevicePtr->PrimIndexPtr(cap.chunk->m_chunkBuf->m_indexBuf);
    g_theGxDevicePtr->Draw(&cap.chunk->m_batch, 1);
}

void CMapRenderChunk::QueueMultiPass(CMapRenderChunk* chunk) {
    CGxBuf* vertex = chunk->m_chunkBuf ? chunk->m_chunkBuf->m_vertexBuf : nullptr;
    CGxBuf* index = chunk->m_chunkBuf ? chunk->m_chunkBuf->m_indexBuf : nullptr;
    int32_t special = (chunk->m_renderFlags & 0x4) != 0;

    for (uint32_t i = 0; i < chunk->m_layersCount; ++i) {
        if (chunk->m_layers[i].m_flags & (0x40 | 0x80)) {
            special = 1;
        }
    }

    if (special || !vertex || !index || !IsVertexSlab(vertex->m_pool) || !IsIndexSlab(index->m_pool) || !vertex->m_itemSize || vertex->m_index % vertex->m_itemSize || index->m_itemSize != 2) {
        CMapRenderChunk::RenderMultiPassAlpha(chunk);
        return;
    }

    TerrainCapture cap = {};
    CGxMatrixStack& world = g_theGxDevicePtr->m_xforms[GxXform_World];
    cap.chunk = chunk;
    cap.world = world.m_mtx[world.m_level];
    C3Vector offset = CWorldScene::s_activeWorldView - chunk->m_vec1;
    CMapRenderChunk::ComputeTexMatrices(cap.tex0, cap.tex1, offset, -CMapChunk::s_geoToTex);
    CaptureLights(cap);
    cap.fogStart = g_theGxDevicePtr->m_appRenderStates[GxRs_FogStart].m_value.m_data.f[0];
    cap.fogEnd = g_theGxDevicePtr->m_appRenderStates[GxRs_FogEnd].m_value.m_data.f[0];
    cap.fogColor = g_theGxDevicePtr->m_appRenderStates[GxRs_FogColor].m_value.m_data.u[0];
    cap.fog = g_theGxDevicePtr->m_appRenderStates[GxRs_Fog].m_value.m_data.i[0];
    cap.indexCount = chunk->m_batch.m_count;
    cap.firstIndex = index->m_index / 2 + chunk->m_batch.m_start;
    cap.baseVertex = static_cast<int32_t>(vertex->m_index / vertex->m_itemSize);
    s_terrainBatch.push_back(cap);
}

void CMapRenderChunk::FlushMultiPass() {
    Profiler::Scope profile(Profiler::TerrainFlush);
    if (s_terrainBatch.empty()) {
        return;
    }

    uint32_t maxLayers = 0;

    for (const TerrainCapture& cap : s_terrainBatch) {
        if (cap.chunk->m_layersCount > maxLayers) {
            maxLayers = cap.chunk->m_layersCount;
        }
    }

    if (s_terrainDraws.capacity() < s_terrainBatch.size()) {
        s_terrainDraws.reserve(s_terrainBatch.size());
    }

    if (s_terrainGroupKeys.capacity() < s_terrainBatch.size()) {
        s_terrainGroupKeys.reserve(s_terrainBatch.size());
    }

    if (s_terrainGroupMembers.capacity() < s_terrainBatch.size()) {
        s_terrainGroupMembers.reserve(s_terrainBatch.size());
    }

    int32_t bindless = g_theGxDevicePtr->TerrainBindless();
    g_theGxDevicePtr->IRsSync(0);

    for (uint32_t layer = 0; layer < maxLayers; ++layer) {
        s_terrainGroupKeys.clear();

        // -- ʕ •ᴥ•ʔ✿ First-seen keys stay in order. A map clear would free every node each layer. ✿ ʕ •ᴥ•ʔ
        for (size_t index = 0; index < s_terrainBatch.size(); ++index) {
            const TerrainCapture& cap = s_terrainBatch[index];

            if (layer >= cap.chunk->m_layersCount) {
                continue;
            }

            TerrainGroupKey key = MakeTerrainGroupKey(cap, layer, bindless);
            size_t group = 0;

            while (group < s_terrainGroupKeys.size() && !(s_terrainGroupKeys[group] == key)) {
                ++group;
            }

            if (group == s_terrainGroupKeys.size()) {
                s_terrainGroupKeys.push_back(key);

                if (s_terrainGroupMembers.size() == group) {
                    s_terrainGroupMembers.emplace_back();
                } else {
                    s_terrainGroupMembers[group].clear();
                }
            }

            s_terrainGroupMembers[group].push_back(index);
        }

        for (size_t group = 0; group < s_terrainGroupKeys.size(); ++group) {
            const std::vector<size_t>& members = s_terrainGroupMembers[group];
            const TerrainCapture& groupFirst = s_terrainBatch[members[0]];
            CMapRenderChunkLayer* key = &groupFirst.chunk->m_layers[layer];

            if (s_meshTaken.size() < members.size()) {
                s_meshTaken.resize(members.size());
            }

            std::fill(s_meshTaken.begin(), s_meshTaken.begin() + static_cast<std::ptrdiff_t>(members.size()), 0);

            for (size_t start = 0; start < members.size(); ++start) {
                if (s_meshTaken[start]) {
                    continue;
                }

                const TerrainCapture& first = s_terrainBatch[members[start]];
                CGxPool* vertexPool = first.chunk->m_chunkBuf->m_vertexBuf->m_pool;
                CGxPool* indexPool = first.chunk->m_chunkBuf->m_indexBuf->m_pool;
                s_terrainDraws.clear();
                std::vector<size_t> queued;

                for (size_t n = start; n < members.size(); ++n) {
                    if (s_meshTaken[n]) {
                        continue;
                    }

                    const TerrainCapture& cap = s_terrainBatch[members[n]];
                    CGxBuf* vertexBuf = cap.chunk->m_chunkBuf->m_vertexBuf;
                    CGxBuf* indexBuf = cap.chunk->m_chunkBuf->m_indexBuf;

                    if (vertexBuf->m_pool != vertexPool || indexBuf->m_pool != indexPool) {
                        continue;
                    }

                    s_meshTaken[n] = 1;
                    CMapRenderChunkLayer* chunkLayer = &cap.chunk->m_layers[layer];
                    GxTerrainDraw draw = {};
                    FillTerrainDraw(draw, cap);

                    if (bindless) {
                        CGxTex* color = TextureGetGxTex(chunkLayer->m_texture, 0, nullptr);

                        if (color) {
                            GxTexSetWrap(color, GxTex_Wrap, GxTex_Wrap);
                        }

                        draw.texIndex0 = g_theGxDevicePtr->TerrainTextureSlot(color);

                        if (chunkLayer->m_layerTexture) {
                            CGxTex* alpha = TextureGetGxTex(chunkLayer->m_layerTexture, 1, nullptr);
                            draw.texIndex1 = g_theGxDevicePtr->TerrainTextureSlot(alpha);
                        } else {
                            draw.texIndex1 = ~0u;
                        }

                        if (draw.texIndex0 == ~0u || (chunkLayer->m_layerTexture && draw.texIndex1 == ~0u)) {
                            DrawTerrainChunk(cap);
                            continue;
                        }
                    }

                    s_terrainDraws.push_back(draw);
                    queued.push_back(members[n]);
                }

                if (s_terrainDraws.empty()) {
                    continue;
                }

                if (bindless) {
                    CMapRenderChunkLayer* bound = &first.chunk->m_layers[layer];
                    CGxTex* color = TextureGetGxTex(bound->m_texture, 0, nullptr);
                    CGxTex* alpha = bound->m_layerTexture ? TextureGetGxTex(bound->m_layerTexture, 1, nullptr) : color;
                    g_theGxDevicePtr->RsSet(GxRs_Texture0, color);
                    g_theGxDevicePtr->RsSet(GxRs_Texture1, alpha ? alpha : color);
                } else {
                    CGxTex* gxTex = TextureGetGxTex(key->m_texture, 0, nullptr);
                    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

                    if (gxTex) {
                        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);
                    }

                    if (key->m_layerTexture) {
                        g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(key->m_layerTexture, 1, nullptr));
                    } else {
                        GxRsSet(GxRs_Texture1, 0);
                    }
                }

                GxRsSet(GxRs_BlendingMode, layer == 0 ? GxBlend_Opaque : GxBlend_Alpha);
                GxUpdateAlphaRef(GxRs_AlphaRef);
                ApplyChunkFog(first);
                GxPrimVertexPtr(first.chunk->m_chunkBuf->m_vertexBuf, CMapRenderChunk::s_gxBufVertexFormat);
                g_theGxDevicePtr->PrimIndexPtr(first.chunk->m_chunkBuf->m_indexBuf);

                constexpr uint32_t kSlice = 16384;

                for (uint32_t offset = 0; offset < s_terrainDraws.size();) {
                    uint32_t n = static_cast<uint32_t>(s_terrainDraws.size() - offset);

                    if (n > kSlice) {
                        n = kSlice;
                    }

                    if (!g_theGxDevicePtr->DrawTerrainBatch(s_terrainDraws.data() + offset, n)) {
                        for (uint32_t i = 0; i < n; i++) {
                            DrawTerrainChunk(s_terrainBatch[queued[offset + i]]);
                        }
                    }

                    offset += n;
                }
            }
        }
    }

    for (const TerrainCapture& cap : s_terrainBatch) {
        if (!cap.chunk->m_shadowTexture || !(CWorld::enables & Enable_Shadow)) {
            continue;
        }

        ApplyChunkFog(cap);
        GxXformSet(GxXform_World, cap.world);
        GxXformSet(GxXform_Tex0, cap.tex0);
        GxXformSet(GxXform_Tex1, cap.tex1);
        GxPrimVertexPtr(cap.chunk->m_chunkBuf->m_vertexBuf, CMapRenderChunk::s_gxBufVertexFormat);
        g_theGxDevicePtr->PrimIndexPtr(cap.chunk->m_chunkBuf->m_indexBuf);
        GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
        GxUpdateAlphaRef(GxRs_AlphaRef);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, TextureGetGxTex(CWorld::shadowModTexture, 1, nullptr));
        g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(cap.chunk->m_shadowTexture, 1, nullptr));
        g_theGxDevicePtr->Draw(&cap.chunk->m_batch, 1);
    }

    s_terrainBatch.clear();
    s_terrainDraws.clear();
    s_terrainGroupKeys.clear();
}

// VERIFY: 18189b69 manual
// OFFSET: 0x007D0D70
void CMapRenderChunk::RenderMultiPassAlpha(CMapRenderChunk* renderChunk) {
    C44Matrix layerMatrix;
    C44Matrix alphaMatrix;
    C3Vector offset = CWorldScene::s_activeWorldView - renderChunk->m_vec1;
    CMapRenderChunk::ComputeTexMatrices(layerMatrix, alphaMatrix, offset, -CMapChunk::s_geoToTex);
    GxXformSet(GxXform_Tex0, layerMatrix);
    GxXformSet(GxXform_Tex1, alphaMatrix);

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    int32_t specular = (renderChunk->m_renderFlags & 0x4) != 0;

    for (int32_t i = 0; i < renderChunk->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &renderChunk->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_ReflectionMap);
            GxRsSet(GxRs_TextureShader0, 0);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 0);
        }

        if (layer->m_flags & 0x40) {
            C3Vector move(World::texVect[layer->m_flags & 7]);
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            move.x *= moveScale;
            move.y *= moveScale;
            move.z *= moveScale;
            GxXformTranslate(GxXform_Tex0, move);
        }

        if (i == 1) {
            GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
            GxUpdateAlphaRef(GxRs_AlphaRef);
        }

        if (layer->m_layerTexture) {
            g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(layer->m_layerTexture, 1, nullptr));
        } else {
            GxRsSet(GxRs_Texture1, 0);
        }

        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_EyeLinear);
            GxRsSet(GxRs_TextureShader0, 1);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 1);
        }

        if (layer->m_flags & 0x40) {
            GxXformSet(GxXform_Tex0, layerMatrix);
        }
    }

    if (renderChunk->m_shadowTexture && (CWorld::enables & Enable_Shadow)) {
        GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
        GxUpdateAlphaRef(GxRs_AlphaRef);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, TextureGetGxTex(CWorld::shadowModTexture, 1, nullptr));
        g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(renderChunk->m_shadowTexture, 1, nullptr));
        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);
    }

    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: 48db2dd7 manual
// OFFSET: 0x007D13F0
void CMapRenderChunk::RenderMultiPassPixelShaderBigAlpha(CMapRenderChunk* renderChunk) {
    C44Matrix layerMatrix;
    C44Matrix alphaMatrix;
    C3Vector offset = CWorldScene::s_activeWorldView - renderChunk->m_vec1;
    CMapRenderChunk::ComputeTexMatrices(layerMatrix, alphaMatrix, offset, -CMapChunk::s_geoToTex);
    GxXformSet(GxXform_Tex0, layerMatrix);
    GxXformSet(GxXform_Tex1, alphaMatrix);

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(renderChunk->m_terrainBlendTexture, 1, nullptr));

    int32_t specular = (renderChunk->m_renderFlags & 0x4) != 0;
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, CMapRenderChunk::s_pixelShader[1]);

    for (int32_t i = 0; i < renderChunk->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &renderChunk->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_ReflectionMap);
            GxRsSet(GxRs_TextureShader0, 0);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 0);
        }

        if (layer->m_flags & 0x40) {
            C3Vector move(World::texVect[layer->m_flags & 7]);
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            move.x *= moveScale;
            move.y *= moveScale;
            move.z *= moveScale;
            GxXformTranslate(GxXform_Tex0, move);
        }

        if (i == 1) {
            GxRsSet(GxRs_BlendingMode, GxBlend_NoAlphaAdd);
            GxUpdateAlphaRef(GxRs_AlphaRef);

            if (g_theGxDevicePtr->Caps().unk_0134) {
                CImVector black;
                black.b = 0;
                black.g = 0;
                black.r = 0;
                black.a = 0;
                GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&black));
            } else {
                C4Vector black;
                black.x = 0.0f;
                black.y = 0.0f;
                black.z = 0.0f;
                black.w = 0.0f;
                GxShaderConstantsSet(GxSh_Pixel, 2, &black.x, 1);
            }

            g_theGxDevicePtr->RsSet(GxRs_PixelShader, CMapRenderChunk::s_pixelShader[0]);
        }

        GxShaderConstantsSet(GxSh_Pixel, 0, s_layerChannel[i], 1);
        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_EyeLinear);
            GxRsSet(GxRs_TextureShader0, 1);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 1);
        }

        if (layer->m_flags & 0x40) {
            GxXformSet(GxXform_Tex0, layerMatrix);
        }
    }

    if (g_theGxDevicePtr->Caps().unk_0134) {
        GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&CMapRenderChunk::s_fogColor));
    } else {
        C4Vector fogColor(CMapRenderChunk::s_fogColor);
        GxShaderConstantsSet(GxSh_Pixel, 2, &fogColor.x, 1);
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));
    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: fd0515a3 manual
// OFFSET: 0x007D1AD0
void CMapRenderChunk::RenderMultiPassPixelShader(CMapRenderChunk* renderChunk) {
    C44Matrix layerMatrix;
    C44Matrix alphaMatrix;
    C3Vector offset = CWorldScene::s_activeWorldView - renderChunk->m_vec1;
    CMapRenderChunk::ComputeTexMatrices(layerMatrix, alphaMatrix, offset, -CMapChunk::s_geoToTex);
    GxXformSet(GxXform_Tex0, layerMatrix);
    GxXformSet(GxXform_Tex1, alphaMatrix);

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(renderChunk->m_terrainBlendTexture, 1, nullptr));

    int32_t specular = (renderChunk->m_renderFlags & 0x4) != 0;
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, CMapRenderChunk::s_pixelShader[1]);

    for (int32_t i = 0; i < renderChunk->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &renderChunk->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_ReflectionMap);
            GxRsSet(GxRs_TextureShader0, 0);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 0);
        }

        if (layer->m_flags & 0x40) {
            C3Vector move(World::texVect[layer->m_flags & 7]);
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            move.x *= moveScale;
            move.y *= moveScale;
            move.z *= moveScale;
            GxXformTranslate(GxXform_Tex0, move);
        }

        if (i == 1) {
            GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
            GxUpdateAlphaRef(GxRs_AlphaRef);
            g_theGxDevicePtr->RsSet(GxRs_PixelShader, CMapRenderChunk::s_pixelShader[0]);
        }

        GxShaderConstantsSet(GxSh_Pixel, 0, s_layerChannel[i], 1);
        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_EyeLinear);
            GxRsSet(GxRs_TextureShader0, 1);
        }

        if (layer->m_flags & 0x80) {
            GxRsSet(GxRs_Lighting, 1);
        }

        if (layer->m_flags & 0x40) {
            GxXformSet(GxXform_Tex0, layerMatrix);
        }
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));
    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: 136fc4a9 manual
// OFFSET: 0x007D20A0
void CMapRenderChunk::RenderMultiPassVertexPixelShaderBigAlpha(CMapRenderChunk* renderChunk) {
    SVertexShaderConstants& consts = CMapRenderChunk::s_vertexShaderConsts;
    C4Vector white;
    white.x = 1.0f;
    white.y = 1.0f;
    white.z = 1.0f;
    white.w = 1.0f;

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(renderChunk->m_terrainBlendTexture, 1, nullptr));

    CGxShader* pixelShader = CMapRenderChunk::s_pixelShader[1];
    int32_t specular = (renderChunk->m_renderFlags & 0x4) != 0;
    CMapRenderChunk::SetVertexShader(renderChunk, 2, specular);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixelShader);

    for (int32_t i = 0; i < renderChunk->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &renderChunk->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (layer->m_flags & 0x80) {
            GxShaderConstantsSet(GxSh_Vertex, 25, &white.x, 1);
        }

        if (layer->m_flags & 0x40) {
            C4Vector dir = World::texVect[layer->m_flags & 7];
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            consts.m_texOffset[0].x = -(dir.x * moveScale * 0.125f);
            consts.m_texOffset[0].y = -(dir.y * moveScale * 0.125f);
            GxShaderConstantsSet(GxSh_Vertex, 13, &consts.m_texOffset[0].x, 1);
        }

        if (i == 1) {
            GxRsSet(GxRs_BlendingMode, GxBlend_NoAlphaAdd);
            GxUpdateAlphaRef(GxRs_AlphaRef);

            if (g_theGxDevicePtr->Caps().unk_0134) {
                CImVector black;
                black.b = 0;
                black.g = 0;
                black.r = 0;
                black.a = 0;
                GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&black));
            } else {
                C4Vector black;
                black.x = 0.0f;
                black.y = 0.0f;
                black.z = 0.0f;
                black.w = 0.0f;
                GxShaderConstantsSet(GxSh_Pixel, 2, &black.x, 1);
            }

            g_theGxDevicePtr->RsSet(GxRs_PixelShader, CMapRenderChunk::s_pixelShader[0]);

            if (specular) {
                CMapRenderChunk::SetVertexShader(renderChunk, 2, 0);
            }
        }

        GxShaderConstantsSet(GxSh_Pixel, 0, s_layerChannel[i], 1);
        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);

        if (layer->m_flags & 0x80) {
            GxShaderConstantsSet(GxSh_Vertex, 25, &consts.m_sunAmbient.x, 1);
        }

        if (layer->m_flags & 0x40) {
            consts.m_texOffset[0].x = 0.0f;
            consts.m_texOffset[0].y = 0.0f;
            GxShaderConstantsSet(GxSh_Vertex, 13, &consts.m_texOffset[0].x, 1);
        }
    }

    if (g_theGxDevicePtr->Caps().unk_0134) {
        GxRsSet(GxRs_FogColor, *reinterpret_cast<int32_t*>(&CMapRenderChunk::s_fogColor));
    } else {
        C4Vector fogColor(CMapRenderChunk::s_fogColor);
        GxShaderConstantsSet(GxSh_Pixel, 2, &fogColor.x, 1);
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));
    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: a31b22da manual
// OFFSET: 0x007D2520
void CMapRenderChunk::RenderMultiPassVertexPixelShader(CMapRenderChunk* renderChunk) {
    SVertexShaderConstants& consts = CMapRenderChunk::s_vertexShaderConsts;
    C4Vector white;
    white.x = 1.0f;
    white.y = 1.0f;
    white.z = 1.0f;
    white.w = 1.0f;

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    g_theGxDevicePtr->RsSet(GxRs_Texture1, TextureGetGxTex(renderChunk->m_terrainBlendTexture, 1, nullptr));

    CGxShader* pixelShader = CMapRenderChunk::s_pixelShader[1];
    int32_t specular = (renderChunk->m_renderFlags & 0x4) != 0;
    CMapRenderChunk::SetVertexShader(renderChunk, 2, specular);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixelShader);

    for (int32_t i = 0; i < renderChunk->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &renderChunk->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (layer->m_flags & 0x80) {
            GxShaderConstantsSet(GxSh_Vertex, 25, &white.x, 1);
        }

        if (layer->m_flags & 0x40) {
            C4Vector dir = World::texVect[layer->m_flags & 7];
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            consts.m_texOffset[0].x = -(dir.x * moveScale * 0.125f);
            consts.m_texOffset[0].y = -(dir.y * moveScale * 0.125f);
            GxShaderConstantsSet(GxSh_Vertex, 13, &consts.m_texOffset[0].x, 1);
        }

        if (i == 1) {
            GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
            GxUpdateAlphaRef(GxRs_AlphaRef);
            g_theGxDevicePtr->RsSet(GxRs_PixelShader, CMapRenderChunk::s_pixelShader[0]);

            if (specular) {
                CMapRenderChunk::SetVertexShader(renderChunk, 2, 0);
            }
        }

        GxShaderConstantsSet(GxSh_Pixel, 0, s_layerChannel[i], 1);
        g_theGxDevicePtr->Draw(&renderChunk->m_batch, 1);

        if (layer->m_flags & 0x80) {
            GxShaderConstantsSet(GxSh_Vertex, 25, &consts.m_sunAmbient.x, 1);
        }

        if (layer->m_flags & 0x40) {
            consts.m_texOffset[0].x = 0.0f;
            consts.m_texOffset[0].y = 0.0f;
            GxShaderConstantsSet(GxSh_Vertex, 13, &consts.m_texOffset[0].x, 1);
        }
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<CGxShader*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<CGxShader*>(nullptr));
    GxRsSet(GxRs_Texture0, 0);
    GxRsSet(GxRs_Texture1, 0);
}

// VERIFY: 22069b93 manual
// OFFSET: 0x007D28B0
void CMapRenderChunk::RenderSinglePassPixelShader() {
    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    C44Matrix layerMatrix;
    C44Matrix alphaMatrix;
    C3Vector offset = CWorldScene::s_activeWorldView - this->m_vec1;
    CMapRenderChunk::ComputeTexMatrices(layerMatrix, alphaMatrix, offset, -CMapChunk::s_geoToTex);

    if (this->m_renderFlags & 0x1) {
        float lighting[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        for (uint32_t i = 0; i < this->m_layersCount; i++) {
            if (this->m_layers[i].m_flags & 0x80) {
                lighting[i] = 0.0f;
            }
        }

        GxShaderConstantsSet(GxSh_Pixel, 1, lighting, 1);
    }

    int32_t specular = (this->m_renderFlags & 0x4) != 0;
    int32_t i = 0;

    for (; i < this->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &this->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (i == 0 && specular) {
            GxRsSet(GxRs_TexGen0, GxTexGen_ReflectionMap);
            GxRsSet(GxRs_TextureShader0, 0);
        } else {
            GxXformSet(static_cast<EGxXform>(GxXform_Tex0 + i), layerMatrix);

            if (layer->m_flags & 0x40) {
                C4Vector move = World::texVect[layer->m_flags & 7];
                float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
                move.x *= moveScale;
                move.y *= moveScale;
                move.z *= moveScale;
                move.w *= moveScale;
                GxXformTranslate(static_cast<EGxXform>(GxXform_Tex0 + i), C3Vector(move));
            }
        }
    }

    g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), TextureGetGxTex(this->m_terrainBlendTexture, 1, nullptr));
    GxXformSet(static_cast<EGxXform>(GxXform_Tex0 + i), alphaMatrix);
    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    for (; i >= 0; i--) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
    }
}

// VERIFY: cad126b2 manual
// OFFSET: 0x007D2D70
void CMapRenderChunk::RenderSinglePassVertexPixelShader() {
    SVertexShaderConstants& consts = CMapRenderChunk::s_vertexShaderConsts;

    GxRsSet(GxRs_BlendingMode, GxBlend_Opaque);
    GxUpdateAlphaRef(GxRs_AlphaRef);

    if (this->m_renderFlags & 0x1) {
        float lighting[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        for (uint32_t i = 0; i < this->m_layersCount; i++) {
            if (this->m_layers[i].m_flags & 0x80) {
                lighting[i] = 0.0f;
            }
        }

        GxShaderConstantsSet(GxSh_Pixel, 1, lighting, 1);
    }

    int32_t specular = (this->m_renderFlags & 0x4) != 0;
    int32_t i = 0;

    for (; i < this->m_layersCount; i++) {
        CMapRenderChunkLayer* layer = &this->m_layers[i];
        CGxTex* gxTex = TextureGetGxTex(layer->m_texture, 0, nullptr);
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), gxTex);
        GxTexSetWrap(gxTex, GxTex_Wrap, GxTex_Wrap);

        if (layer->m_flags & 0x40) {
            C4Vector dir = World::texVect[layer->m_flags & 7];
            float moveScale = 1.0f / CMapChunk::s_geoToTex / s_texAnimRate[(layer->m_flags >> 3) & 7];
            consts.m_texOffset[i].x = -(dir.x * moveScale * 0.125f);
            consts.m_texOffset[i].y = -(dir.y * moveScale * 0.125f);
        }
    }

    CGxTex* blendTex = this->m_terrainBlendTexture ? TextureGetGxTex(this->m_terrainBlendTexture, 1, nullptr) : nullptr;
    g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), blendTex);

    CMapRenderChunk::SetVertexShader(this, this->m_layersCount + 1, specular);
    g_theGxDevicePtr->Draw(&this->m_batch, 1);

    for (uint32_t j = 0; j < this->m_layersCount; j++) {
        if (this->m_layers[j].m_flags & 0x40) {
            consts.m_texOffset[j].x = 0.0f;
            consts.m_texOffset[j].y = 0.0f;
            GxShaderConstantsSet(GxSh_Vertex, 13 + j, &consts.m_texOffset[j].x, 1);
        }
    }

    for (; i >= 0; i--) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + i), static_cast<CGxTex*>(nullptr));
    }
}

// VERIFY: 1f2ad08e manual
// OFFSET: 0x007D3E10
void CMapRenderChunk::SetShaders(int32_t a1, int32_t a2) {
    CMapRenderChunk::s_pixelShader[1] = nullptr;
    CMapRenderChunk::s_pixelShader[0] = nullptr;
    CMapRenderChunk::s_currentShaderX[0] = nullptr;
    CMapRenderChunk::s_currentShaderX[1] = nullptr;
    CMapRenderChunk::s_currentShaderX[2] = nullptr;
    CMapRenderChunk::s_currentShaderX[3] = nullptr;
    CMapRenderChunk::s_currentShader[0] = nullptr;
    CMapRenderChunk::s_currentShader[1] = nullptr;
    CMapRenderChunk::s_currentShader[2] = nullptr;
    CMapRenderChunk::s_currentShader[3] = nullptr;

    if (!CMap::gTerrainPixelShadersValid) {
        if (CMap::header.m_flags & 0x4) {
            CMapRenderChunk::s_renderLayersFunc = CMapRenderChunk::RenderMultiPassAdditive;
        } else {
            CMapRenderChunk::s_renderLayersFunc = CMapRenderChunk::RenderMultiPassAlpha;
        }

        return;
    }

    int32_t bigAlpha = (CMap::header.m_flags >> 2) & 1;
    int32_t shadowMode = CShadowCache::GetShadowValue();
    CMapRenderChunk::s_pixelShader[1] = CMap::GetPixelShader(bigAlpha, true, a1);
    CMapRenderChunk::s_pixelShader[0] = CMap::GetPixelShader(bigAlpha, false, a1);

    for (int32_t layers = 1; layers <= 4; layers++) {
        CGxShader* shader = CMap::GetTerrainPixelShader(bigAlpha, layers, shadowMode, a1, a2);

        if (layers <= g_theGxDevicePtr->Caps().m_numTmus && shader && shader->Valid()) {
            CMapRenderChunk::s_currentShaderX[layers - 1] = shader;
        }
    }

    if (CMap::header.m_flags & 0x4) {
        if (CMap::enableTerrainShaderVertex) {
            CMapRenderChunk::s_renderLayersFunc = CMapRenderChunk::RenderMultiPassVertexPixelShaderBigAlpha;
        } else {
            CMapRenderChunk::s_renderLayersFunc = CMapRenderChunk::RenderMultiPassPixelShaderBigAlpha;
        }
    } else {
        CMapRenderChunk::s_renderLayersFunc = CMapRenderChunk::RenderMultiPassVertexPixelShader;

        if (!CMap::enableTerrainShaderVertex) {
            CMapRenderChunk::s_renderLayersFunc = CMapRenderChunk::RenderMultiPassPixelShader;
        }
    }
}

// VERIFY: dba386e8 manual
// OFFSET: 0x005EEB70
void CMapRenderChunk::RenderZoneDebug() {
}
