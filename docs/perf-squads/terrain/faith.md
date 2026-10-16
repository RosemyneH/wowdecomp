# Faith — Campaign 3 terrain multipass

Reviewed the live batch path. No writer diff. A faster texture group is in bounds only when it draws the same pairs as the nested scan.

**Verdict: PASS** on batch membership, alpha order, shadow-after-batches, and `GxTerrainDraw` field order.

**FAIL** two layout shortcuts if landed as written: a group key of `(texture, layer texture)` alone, and one shared light block for the whole flush.

## Batch membership — PASS

`QueueMultiPass` is the only entry. It runs only for the fixed-function alpha path, when the shader slot for that layer-count bucket is empty and `s_renderLayersFunc` is `RenderMultiPassAlpha` (`WoW/Source/WorldClient/World.cpp:3704`). Shader chunks and the other multipass functions never enter `s_terrainBatch`.

A chunk is pushed only when every gate at `WoW/Source/WorldClient/MapRenderChunk.cpp:2537` is false. Otherwise it draws immediately through `RenderMultiPassAlpha` (`MapRenderChunk.cpp:2538`) and is absent from the flush.

| Gate | Site | Stays out of the batch |
| --- | --- | --- |
| Specular | `MapRenderChunk.cpp:2529` | `m_renderFlags & 0x4` |
| Animated or unlit layer | `MapRenderChunk.cpp:2531` | any layer with `m_flags & (0x40 \| 0x80)` |
| Missing buffers | `MapRenderChunk.cpp:2527` | null vertex or index buf |
| Pool identity | `MapRenderChunk.cpp:2537` | `vertex->m_pool != s_gxVertexPool` or `index->m_pool != s_gxIndexPool` |
| Vertex packing | `MapRenderChunk.cpp:2537` | `m_itemSize == 0` or `m_index` not a multiple of `m_itemSize` |
| Index width | `MapRenderChunk.cpp:2537` | `index->m_itemSize != 2` |

Pool identity is pointer equality with the two terrain pools. A streaming buf from `UseStreamingBufs` fails that test and stays on the immediate path.

`FlushMultiPass` runs at the end of each layer-count list, still inside that pass (`World.cpp:3728`). It does not wait until the end of `RenderChunks`.

## Alpha layer order — PASS

`FlushMultiPass` walks layers `0 .. maxLayers-1` (`MapRenderChunk.cpp:2576`). `used` is cleared at the start of each layer (`MapRenderChunk.cpp:2577`), so a chunk may join a different pair on the next layer and may not join a later layer it does not have (`MapRenderChunk.cpp:2580`).

Blend is opaque on layer 0 and alpha on every later layer (`MapRenderChunk.cpp:2619`). That matches `RenderMultiPassAlpha`, which switches to alpha at layer 1 and leaves it there (`MapRenderChunk.cpp:2697`).

Within a layer, groups start at the first unused chunk and run in increasing batch index. A regroup that draws layer 1 before layer 0, or that mixes layers into one group, fails.

## Shadow pass after batches — PASS

The shadow loop is after the layer loop closes and before `s_terrainBatch.clear()` (`MapRenderChunk.cpp:2637`). It walks the same captures, in queue order. A chunk is skipped unless `m_shadowTexture` is set and `Enable_Shadow` is on (`MapRenderChunk.cpp:2638`). The draw is alpha, `shadowModTexture` on stage 0, the chunk shadow on stage 1 (`MapRenderChunk.cpp:2648`).

Chunks that failed the membership gate already drew their shadow inside `RenderMultiPassAlpha` (`MapRenderChunk.cpp:2724`) and are not in this loop. An empty batch returns before the shadow loop (`MapRenderChunk.cpp:2560`); those chunks are the immediate path.

Shadow draws are not texture pairs. They stay one draw per batched chunk, after every layer group of that flush.

## GxTerrainDraw field order — PASS

CPU record (`Engine/Source/Gx/CGxDevice/CGxDevice.h:72`), GPU record (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:89`), and the shader (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:18`) use the same prefix. `C44Matrix` is sixteen floats, 64 bytes (`Engine/Source/Tempest/C44Matrix.h:23`).

| Byte | `GxTerrainDraw` | `GpuDrawItem` / `Ffp.vert` |
| --- | --- | --- |
| 0 | `world` | `world` |
| 64 | `tex0` | `tex0` |
| 128 | `tex1` | `tex1` |
| 192 | `lightPos[4][4]` | `lightPos` |
| 256 | `lightDiffuse[4][4]` | `lightDiffuse` |
| 320 | `lightAmbient[4][4]` | `lightAmbient` |
| 384 | `lightAtten[4][4]` | `lightAtten` |
| 448 | `indexCount`, `firstIndex`, `baseVertex` | not in the 448-byte record |

Pins: `sizeof(GpuDrawItem) == 448`, `tex0` at 64, `tex1` at 128, `lightPos` at 192, `lightAtten` at 384 (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:20`).

`FillTerrainDraw` writes that order (`MapRenderChunk.cpp:2500`). `DrawTerrainBatch` copies the same fields into `GpuDrawItem` and puts the three draw args on the indirect command (`CGxDeviceVulkan.cpp:1516`). The base device returns 0 (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:146`), so D3D never reads this struct.

Do not insert, drop, or reorder fields. The trailing draw args stay after the light block.

**Per-draw lights: PASS** as copied today. **One light block per flush: FAIL.**

`RenderSetup` selects lights and fog for that chunk, then calls `SetupGxLights` / `SetupGxFog`, before the chunk is queued (`MapRenderChunk.cpp:923`). Point lights are the chunk sphere’s set (`Engine/Source/Model2/M2Light.cpp:337`). `CaptureLights` copies `m_lights` into that capture (`MapRenderChunk.cpp:2463`). `FillTerrainDraw` copies those words into the draw (`MapRenderChunk.cpp:2507`). Two chunks in one flush can disagree. A shared flush light changes `GpuDrawItem` contents.

## Texture pairs — PASS on the current scan

Both the indirect build and the `DrawTerrainBatch == 0` fallback use one predicate (`MapRenderChunk.cpp:2596` and `MapRenderChunk.cpp:2629`):

- same layer index
- `m_texture` pointer equal
- `m_layerTexture` pointer equal
- `fog` equal
- `fogColor` equal
- `fogStart` equal (`float ==`, not an epsilon)
- `fogEnd` equal (`float ==`)

World matrix, tex matrices, lights, and the index range ride along inside the pair. They do not split it. Each chunk is in at most one pair per layer (`used[j] = 1` at `MapRenderChunk.cpp:2600`).

Order that must survive a sort or a hash:

1. Layers ascend.
2. Pairs in a layer follow the first unused chunk that owns that key (batch order).
3. Draws inside a pair follow increasing batch index.
4. The fallback draws that same sequence one chunk at a time when `DrawTerrainBatch` returns 0 (`MapRenderChunk.cpp:2625`).

**A key of `(texture, layer texture)` alone: FAIL.** It merges captures the nested scan keeps apart whenever fog, fog color, fog start, or fog end differ. Pointer identity stays; texture ids are not the key. Hash iteration order is not pair order.
