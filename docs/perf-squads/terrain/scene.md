# Campaign 3 scene — outdoor multipass

Scene hold for the terrain squad. The camera is the Campaign 0 outdoor shot: Elwynn / Westfall density, same character position and camera every run. The check is the fixed-function multipass flush (`s_renderLayersFunc == RenderMultiPassAlpha`).

## Frame

`CWorldScene::RenderChunksSinglePass` walks layer-count buckets, queues each chunk, and flushes the bucket:

- Queue: `WoW/Source/WorldClient/World.cpp:3710-3711`
- Flush: `WoW/Source/WorldClient/World.cpp:3728`

`RenderSetup` runs first (`WoW/Source/WorldClient/World.cpp:3701`) and writes the world matrix, lights, and fog that `QueueMultiPass` copies (`WoW/Source/WorldClient/MapRenderChunk.cpp:910-927`, `WoW/Source/WorldClient/MapRenderChunk.cpp:2542-2555`).

## Must match

### Layer textures

A batch binds one pair and only accepts chunks with that pair.

- Key: `m_texture` and `m_layerTexture` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2596`; fields at `WoW/Source/WorldClient/MapRenderChunk.h:55` and `WoW/Source/WorldClient/MapRenderChunk.h:57`).
- Bind: texture 0 from `m_texture`, texture 1 from `m_layerTexture` or cleared (`WoW/Source/WorldClient/MapRenderChunk.cpp:2606-2617`).
- Texture 0 wrap stays `GxTex_Wrap` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2610`).

Chunks may be reordered inside one layer. Each draw still uses that chunk’s own layer textures. Immediate chunks that miss the batch bind the same fields in `RenderMultiPassAlpha` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2675-2706`).

### Alpha order

Layers run `0 .. maxLayers-1` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2576`). Layer 0 is `GxBlend_Opaque`. Every later layer is `GxBlend_Alpha` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2619`; enums at `Engine/Source/Gx/Types.h:29` and `Engine/Source/Gx/Types.h:31`). The immediate path uses the same split: opaque, then alpha from layer 1 (`WoW/Source/WorldClient/MapRenderChunk.cpp:2668`, `WoW/Source/WorldClient/MapRenderChunk.cpp:2697-2699`). A sort or hash on `(texture, layer texture)` stays inside one layer. Layer 0 still draws before later layers.

### Shadows

The shadow draw stays after that flush’s layer batches and before `s_terrainBatch.clear()` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2637-2655`).

- Skip when `m_shadowTexture` is null or `Enable_Shadow` is off (`WoW/Source/WorldClient/MapRenderChunk.cpp:2638`; `WoW/Source/WorldClient/Types.h:48`; field at `WoW/Source/WorldClient/MapRenderChunk.h:142`).
- Blend is `GxBlend_Alpha` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2648-2649`).
- Texture 0 is `CWorld::shadowModTexture`. Texture 1 is the chunk shadow (`WoW/Source/WorldClient/MapRenderChunk.cpp:2650-2651`; `WoW/Source/WorldClient/World.cpp:187`).

`RenderMultiPassAlpha` draws the same shadow after its own layers (`WoW/Source/WorldClient/MapRenderChunk.cpp:2724-2729`). Chunks that miss the batch still shadow on that path.

### Batch membership

The outdoor set of batched versus immediate chunks stays put. `QueueMultiPass` sends a chunk to `RenderMultiPassAlpha` when any of these hold (`WoW/Source/WorldClient/MapRenderChunk.cpp:2529-2539`):

- `m_renderFlags & 0x4`
- any layer flag `0x40` or `0x80`
- missing vertex or index buffer
- vertex pool is not `s_gxVertexPool`, or index pool is not `s_gxIndexPool`
- vertex item size is 0, the vertex index is not a multiple of that size, or the index item size is not 2

The shaded record stays the 448-byte `GpuDrawItem` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:20`; `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:88-97`). `DrawTerrainBatch` still copies `world`, `tex0`, `tex1`, and the four light blocks out of `GxTerrainDraw` (`Engine/Source/Gx/CGxDevice/CGxDevice.h:72-83`; `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1516-1524`).

## Pass

### Capture bytes per chunk

`QueueMultiPass` copies a `TerrainCapture` per chunk (`WoW/Source/WorldClient/MapRenderChunk.cpp:2443-2458`, `WoW/Source/WorldClient/MapRenderChunk.cpp:2556`). Field sizes below use the 4-byte pointer from the chunk headers. On this LP64 build the pointer is 8 bytes, so `sizeof(TerrainCapture)` is not 468. The pass is the field set.

| Fields | Bytes | Lines |
| --- | --- | --- |
| `chunk` | 4 | `WoW/Source/WorldClient/MapRenderChunk.cpp:2444` |
| `world`, `tex0`, `tex1` (`C44Matrix` is 16 floats) | 192 | `WoW/Source/WorldClient/MapRenderChunk.cpp:2445-2447`; `Engine/Source/Tempest/C44Matrix.h:21-36` |
| four `float[4][4]` light blocks | 256 | `WoW/Source/WorldClient/MapRenderChunk.cpp:2448-2451`, filled by `CaptureLights` at `WoW/Source/WorldClient/MapRenderChunk.cpp:2463-2490` |
| `fogStart`, `fogEnd`, `fogColor`, `fog` | 16 | `WoW/Source/WorldClient/MapRenderChunk.cpp:2452-2455`, sampled at `WoW/Source/WorldClient/MapRenderChunk.cpp:2549-2552` |
| **prefix through fog** | **468** | |
| `indexCount`, `firstIndex`, `baseVertex` | 12 | `WoW/Source/WorldClient/MapRenderChunk.cpp:2456-2458`, stored at `WoW/Source/WorldClient/MapRenderChunk.cpp:2553-2555` |

`FillTerrainDraw` copies the light blocks again into `GxTerrainDraw` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2507-2510`).

Pass: each chunk stores the three matrices (192 bytes) plus the draw args (12 bytes), and the chunk pointer. The 256-byte light blocks are one block for the flush. The 16-byte fog sample is one block for the flush: `SelectLights` writes the same `DayNight` fog for every chunk (`WoW/Source/WorldClient/MapRenderChunk.cpp:548-554`). `CaptureLights` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2463`) runs once per flush.

That shared light block still has to match this camera. `RenderSetup` calls `SetupGxLights` before each queue (`WoW/Source/WorldClient/MapRenderChunk.cpp:923-926`; `Engine/Source/Model2/M2Light.cpp:316-358`). If two chunks in one outdoor flush do not carry the same four lights, one block fails the screenshot.

### Flush scratch does not allocate after warmup

Every `FlushMultiPass` builds scratch and drops it:

- `std::vector<uint8_t> used(s_terrainBatch.size())` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2572`)
- `std::vector<GxTerrainDraw> draws` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2573`), grown by `push_back` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2603`)
- `s_terrainBatch.push_back` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2556`) until the batch reaches its high-water mark. `clear` at `WoW/Source/WorldClient/MapRenderChunk.cpp:2655` already keeps that vector’s capacity

Pass: `s_terrainBatch`, `used`, and `draws` live across flushes, `clear` and `reserve`, and a steady frame on this camera does not allocate them after the first spike. `draws.clear()` inside the layer loop (`WoW/Source/WorldClient/MapRenderChunk.cpp:2585`) is the reset once `draws` outlives the flush.

Batch count on the outdoor log matches the baseline. The group key stays texture, layer texture, and the flush fog (`WoW/Source/WorldClient/MapRenderChunk.cpp:2596`).

## Screenshot

One outdoor frame at the Campaign 0 camera, after the scratch has warmed. Layer textures, alpha order, and shadows match the baseline. Capture bytes per chunk are the matrices plus the draw args. The flush scratch does not allocate.
