# Campaign 3 — Layout

Records for the multipass flush. Owner is the block `WoW/Source/WorldClient/MapRenderChunk.cpp:2443-2656`. `GxTerrainDraw` and `GpuDrawItem` stay as they are. Sizes are LP64 (pointer 8). `C44Matrix` is 16 floats, **64** bytes (`Engine/Source/Tempest/C44Matrix.h:23-36`).

Three slices, one concern each. Structures below are the end state. Slice 1 lands the slim capture and the flush blocks. Slice 2 makes `used` and `draws` persistent. Slice 3 replaces the nested scan with the hash.

## TerrainCapture

One record per chunk that passes the gate at `MapRenderChunk.cpp:2537-2539`. Chunks that fail still call `RenderMultiPassAlpha` (`MapRenderChunk.cpp:2538`) and are not stored.

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `chunk` | 0 | 8 |
| `world` | 8 | 64 |
| `tex0` | 72 | 64 |
| `tex1` | 136 | 64 |
| `indexCount` | 200 | 4 |
| `firstIndex` | 204 | 4 |
| `baseVertex` | 208 | 4 |
| tail pad | 212 | 4 |
| **sizeof** | | **216** |

Today the same struct is **488** bytes (`docs/perf-squads/terrain/accountant.md`): the 256-byte light arrays and the 16-byte fog tuple sit on every chunk (`MapRenderChunk.cpp:2448-2455`). Those leave this record. `terrain_capture_bytes` adds `sizeof(TerrainCapture)` at `push_back` (`MapRenderChunk.cpp:2556`), so the counter becomes **216**.

`QueueMultiPass` still fills the per-chunk fields from the same sites:

- `chunk` at `MapRenderChunk.cpp:2544`
- `world` from `m_xforms[GxXform_World]` at `MapRenderChunk.cpp:2543-2545`, after `RenderSetup` wrote it (`MapRenderChunk.cpp:916-921`)
- `tex0`, `tex1` from `ComputeTexMatrices` at `MapRenderChunk.cpp:2546-2547` (body `MapRenderChunk.cpp:2331-2363`)
- `indexCount`, `firstIndex`, `baseVertex` at `MapRenderChunk.cpp:2553-2555` (`m_batch.m_count`, `m_index / 2 + m_batch.m_start`, `m_index / m_itemSize`)

`s_terrainBatch` stays the file-scope vector at `MapRenderChunk.cpp:2461`. `clear()` at `MapRenderChunk.cpp:2655` keeps its capacity. Element size changes, so the first `push_back` after the edit allocates the new stride. Later flushes at or under that high-water mark do not.

## One light block and one fog tuple

Both are file-scope, next to `s_terrainBatch`. No heap. Written once per flush, on the first chunk that enters the batch. Later accepted chunks do not call `CaptureLights`.

```cpp
struct TerrainLightBlock {
    float lightPos[4][4];
    float lightDiffuse[4][4];
    float lightAmbient[4][4];
    float lightAtten[4][4];
};

struct TerrainFogBlock {
    float fogStart;
    float fogEnd;
    uint32_t fogColor;
    int32_t fog;
};
```

`TerrainLightBlock` is **256** bytes, alignment 4. Field order matches `GxTerrainDraw` at offset 192 (`Engine/Source/Gx/CGxDevice/CGxDevice.h:76-79`), which is the `GpuDrawItem` prefix from byte 192 (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:93-96`). `TerrainFogBlock` is **16** bytes, the four values read today at `MapRenderChunk.cpp:2549-2552`.

`CaptureLights` (`MapRenderChunk.cpp:2463-2490`) writes a `TerrainLightBlock&`. The `memset`s and the `m_lights[0..3]` loop stay. `ApplyChunkFog` (`MapRenderChunk.cpp:2493-2497`) reads a `TerrainFogBlock`. `DrawTerrainChunk` and the shadow loop pass the flush fog.

A `uint8_t` ready flag, cleared wherever the batch is `clear()`ed, stops a second pack. The first accepted chunk is the sample. A chunk that took `RenderMultiPassAlpha` has already returned and is not the sample.

`RenderSetup` still runs per chunk before the queue (`World.cpp:3701`, `MapRenderChunk.cpp:923-927`). `SelectLights` sets fog from `DayNight::GetInfo` (`MapRenderChunk.cpp:548-554`). `SetupGxLights` writes the sun plus the nearest point lights for that chunk’s sphere (`Engine/Source/Model2/M2Light.cpp:316-358`, `Engine/Source/Model2/M2Scene.cpp:868-907`), all relative to `s_activeWorldView` (`MapRenderChunk.cpp:926`). Under the Campaign 0 flag, each later accepted chunk packs a stack `TerrainLightBlock` and a stack `TerrainFogBlock` and compares them to the flush copies (`memcmp` on the 256 light bytes, `==` on each fog field, including the two floats). A mismatch increments a counter and does not allocate a second block. It does not add a gate at `MapRenderChunk.cpp:2537` and does not put the arrays back on `TerrainCapture`. A non-zero mismatch on the outdoor scene rejects this shared-block approach for the cycle.

With the flag off, `CaptureLights` runs once per flush. Capture store per flush is **216N + 256 + 16**.

## GxTerrainDraw

Unchanged. **460** bytes: the **448**-byte GPU prefix plus `indexCount`, `firstIndex`, `baseVertex` (`CGxDevice.h:72-83`). Pins stay `sizeof(GpuDrawItem) == 448`, `tex0` at 64, `tex1` at 128, `lightPos` at 192, `lightAtten` at 384 (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:20-24`, `Shaders/Ffp.vert:18-26`). No chunk pointer on this struct.

`FillTerrainDraw` (`MapRenderChunk.cpp:2500-2514`):

- `world`, `indexCount`, `firstIndex`, `baseVertex` from the capture
- `GxXformSet` of `tex0` / `tex1`, then `ResolveTexMatrix` into `draw.tex0` / `draw.tex1`, after `IRsSync(0)` (`MapRenderChunk.cpp:2574`, `CGxDeviceVulkan.cpp:929-932`)
- one `memcpy` of the flush `TerrainLightBlock` onto `&draw.lightPos` (256 bytes, contiguous in both structs)

`DrawTerrainBatch` still copies that prefix into each `GpuDrawItem` and writes the three draw args onto the indirect command (`CGxDeviceVulkan.cpp:1516-1529`). The base device still returns 0 (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:146-149`). The 448-byte repeat per draw stays. The cut is the per-chunk CPU copy.

## used and draws

File-scope, same translation unit, process lifetime. `clear()` keeps capacity. No `shrink_to_fit`, no swap with a temporary, no fresh vector per flush.

| Vector | Element | Role |
| --- | --- | --- |
| `s_terrainUsed` | `uint32_t` | Per layer, chunk indices in draw order. Until the hash slice, the nested scan stores 0/1 flags in this same vector. |
| `s_terrainDraws` | `GxTerrainDraw` | The group built for one `DrawTerrainBatch`. `clear()` at each group (`MapRenderChunk.cpp:2585`). |

`reserve` runs only when `capacity` is below the need, and never above the live pool slot count. That count is `(2 * radius * radius + 1) & ~1` with `radius` capped at 160 (`MapRenderChunk.cpp:824-828`), so the ceiling is **51200**. After `UpdatePools`, the live value is `s_chunkBlockArray.Count() * 2` (`MapRenderChunk.cpp:837`). A later larger pool, still inside the cap, may `reserve` once more. A smaller pool does not shrink. `push_back` does not run once `s_terrainBatch.size()` is at that count; the extra chunk takes `RenderMultiPassAlpha`. A zero count means the pools are down; do not push.

`s_terrainDraws.reserve` uses the batch size, so one group that holds every chunk fits. `draws` must not gain a `CMapRenderChunk*`. After `DrawTerrainBatch` returns, the GPU copies already sit in the frame rings (`CGxDeviceVulkan.cpp:1516-1538`).

Empty flush returns at `MapRenderChunk.cpp:2560` before any `reserve`. `s_terrainBatch.clear()` stays after the shadow pass (`MapRenderChunk.cpp:2655`). That `clear()` is the retire for the pointers. Do not walk `capacity`.

The chunk pointer is live only while the capture is in the vector (`docs/perf-squads/terrain/leak.md`). These calls `clear()` the batch, and the ready flag, before buffers die:

- `CMapRenderChunk::Destroy` / `DestroyBufs` before `BufDestroy` and `PoolDestroy` (`MapRenderChunk.cpp:378`, `MapRenderChunk.cpp:397`)
- `CMap::ClearAllRenderChunks` (`WoW/Source/WorldClient/Map.cpp:469`) and `CMap::ProcessRenderChunkUpdateList` (`Map.cpp:1933`) before `FreeBuf`

`~CMapRenderChunk` (`MapRenderChunk.cpp:785`) drops every capture whose `chunk` is `this` before `FreeBuf` (`MapRenderChunk.cpp:787`). Swap-with-last. That runs when the flush is not walking the vector: `RenderChunksSinglePass` queues and flushes a bucket without purging (`World.cpp:3699-3728`). The scrub functions live next to `s_terrainBatch`. The call sites are the list above.

## Texture grouping

Hash, not `std::unordered_map`. Runs are stored in insertion order so pair order stays the nested scan’s order. `std::unordered_map` iteration is not that order.

Per layer, after the `maxLayers` walk is unchanged (`MapRenderChunk.cpp:2564-2576`):

```cpp
struct TerrainTexRun {
    CTexture* texture;
    CTexture* layerTexture;
    uint32_t begin;
    uint32_t count;
};
```

`s_terrainRuns` is a `std::vector<TerrainTexRun>`. `s_terrainHash` is a `std::vector<uint32_t>`, power-of-two slot count, at least `2 * n`, empty slot `0xFFFFFFFFu`. Same lifetime as `s_terrainUsed`: grow toward the pool cap, never shrink, `std::fill` to empty at the start of each layer. `s_terrainRuns.clear()` and `s_terrainUsed.clear()` per layer.

Key is pointer equality on `CMapRenderChunkLayer::m_texture` and `m_layerTexture` (`MapRenderChunk.h:55`, `MapRenderChunk.h:57`), the same pointers the predicate compares (`MapRenderChunk.cpp:2596`). A null `m_layerTexture` is a real key. Hash:

`uintptr_t` of the two pointers, `(x * 0x9E3779B97F4A7C15ull) ^ (y >> 4)`, then the low bits select the slot. Linear probe.

Fog is the flush tuple, compared once at queue. It is not a per-run hash input. Every run in the flush shares `s_terrainFog`, so the partition matches the predicate at `MapRenderChunk.cpp:2596` and `MapRenderChunk.cpp:2629` while that tuple is the only fog in the batch.

Two passes, both in increasing batch index:

1. Count. Skip `layer >= m_layersCount`. Find or append a run. Appending is first-seen order. Increment `count`.
2. Prefix-sum `begin`, then `s_terrainUsed.resize` to the sum (no alloc when capacity holds). Scatter chunk indices. The second pass only looks up runs.

Each run then builds `s_terrainDraws` from `s_terrainUsed[begin, begin + count)` and draws. Bind state stays the current group setup (`MapRenderChunk.cpp:2606-2623`): texture 0 from the run’s `m_texture`, wrap `GxTex_Wrap`, texture 1 from `m_layerTexture` or cleared, blend from the layer index, fog from `s_terrainFog`, vertex and index buffers from the first chunk in the run (the old key index `i`). That chunk’s buffers are in `s_gxVertexPool` / `s_gxIndexPool`, so `firstIndex` and `baseVertex` address the same pool buffer `DrawTerrainBatch` binds (`CGxDeviceVulkan.cpp:1525-1528`).

When `DrawTerrainBatch` returns 0, `DrawTerrainChunk` walks that same index range. It does not evaluate the predicate again.

Order that the hash has to keep:

1. Layers ascend. Layer 0 is `GxBlend_Opaque`. Later layers are `GxBlend_Alpha` (`MapRenderChunk.cpp:2619`).
2. Runs in a layer follow the first chunk that owns that texture pair.
3. Draws inside a run follow increasing batch index.
4. The fallback draws that same sequence, one chunk at a time.

The shadow pass stays after the layer loop and before `clear()` (`MapRenderChunk.cpp:2637-2653`). It walks `s_terrainBatch` in queue order, not run order. One `Draw` per chunk that has `m_shadowTexture` and `Enable_Shadow`. It does not build a `GxTerrainDraw`.

## Slices

1. **Slim capture and shared lights.** Replace the fields on `TerrainCapture`. Add `TerrainLightBlock`, `TerrainFogBlock`, and the ready flag. `CaptureLights` once per flush. `FillTerrainDraw` memcpy’s that block. Fog for the pair test, `ApplyChunkFog`, and the shadow pass comes from `TerrainFogBlock`. Nested scan, local `used`, and local `draws` stay. Campaign 0 compare as above.
2. **Persistent scratch.** `s_terrainUsed` and `s_terrainDraws` become the file-scope vectors. `reserve` and the retire calls from the leak list. The nested scan still decides pairs.
3. **Texture grouping.** Add `s_terrainRuns` and `s_terrainHash`. `s_terrainUsed` becomes the packed index list. Delete the `j` loops at `MapRenderChunk.cpp:2587` and `MapRenderChunk.cpp:2626`.

## Must stay

- Gate at `MapRenderChunk.cpp:2529-2539`: `m_renderFlags & 0x4`, any layer `m_flags & (0x40 | 0x80)`, null buffers, pool identity, vertex packing, index item size 2.
- `FlushMultiPass` once per pass / layer-count bucket (`World.cpp:3691-3728`).
- Shadow after the layer batches, queue order, same skip and textures (`MapRenderChunk.cpp:2637-2651`).
- `GxTerrainDraw` field order, `GpuDrawItem`, `Ffp.vert`, `DrawTerrainBatch`’s copies.
- `RenderSetup`, `RenderMultiPassAlpha`, `ComputeTexMatrices`, and the call sites in `World.cpp` stay. New comments in this block use the repo TextEmoji form.
