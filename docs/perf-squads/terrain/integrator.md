# Integrator — terrain

Campaign 3. **Remesh** (Campaign 4) and **scratch** (Campaign 5) also edit `MapRenderChunk.cpp`. This squad owns the multipass record: `QueueMultiPass`, `FlushMultiPass`, `TerrainCapture`, `CaptureLights`.

`used` and `draws` inside `FlushMultiPass` are this squad's flush scratch. The scratch squad is the visibility, owned-fill, and doodad-cull campaign.

Group D is campaign 3, then campaign 4 (`docs/perf-agent-plan.md:48`). Group C is campaign 5 (`docs/perf-agent-plan.md:47`). Those groups can share a machine. This file cannot. One branch owns `MapRenderChunk.cpp` at merge time.

## Merge order

1. **terrain** — land and close first.
2. **scratch** — rebase onto closed terrain.
3. **remesh** — starts when terrain closes. Rebase onto terrain plus scratch.

`MapChunk.cpp` uses the same order: scratch's visibility queue, then remesh's `CreateIndices`. `World.cpp` is scratch only. `DetailDoodad.cpp` is remesh only.

A conflict hunk that crosses two owners is split on the ranges below. The owner of a function keeps that function.

## Terrain may edit

Only this block in `WoW/Source/WorldClient/MapRenderChunk.cpp`:

| Function | Lines |
| --- | --- |
| `TerrainCapture` | 2443–2459 |
| `s_terrainBatch` | 2461 |
| `CaptureLights` | 2463–2491 |
| `ApplyChunkFog` | 2493–2498 |
| `FillTerrainDraw` | 2500–2514 |
| `DrawTerrainChunk` | 2516–2524 |
| `QueueMultiPass` | 2526–2557 |
| `FlushMultiPass` | 2559–2656 |

`FlushMultiPass` owns `used` (`2572`), `draws` (`2573`), the layer walk, and the shadow pass (`2637–2653`). Layer 0 stays the opaque blend (`2619`). Chunks with `special` flags, a missing buffer, or a foreign pool still take `RenderMultiPassAlpha` (`2529–2539`).

These stay as they are, and terrain leaves them:

| What | Where |
| --- | --- |
| Declarations `QueueMultiPass` / `FlushMultiPass` | `WoW/Source/WorldClient/MapRenderChunk.h:101–102` |
| Call sites | `WoW/Source/WorldClient/World.cpp:3711`, `World.cpp:3728` |
| `ComputeTexMatrices` (called from `QueueMultiPass`) | `MapRenderChunk.cpp:2331–2363`, call at `2547` |
| `RenderMultiPassAlpha` | `MapRenderChunk.cpp:2660`, call at `2538` |
| `GxTerrainDraw` field order | `Engine/Source/Gx/CGxDevice/CGxDevice.h:72–83` |

## Scratch may edit

### Owned-fill scratch — `MapRenderChunk.cpp`

| Function | Lines |
| --- | --- |
| `OwnedFill` | 140–146 |
| `s_ownedFills` | 148 |
| `RangesOverlap` | 150–158 |
| `FillOwnedRange` | 198–204 |
| `CancelOwnedFill` | 206–214 |
| `ClearOwnedFills` | 216–218 |
| `QueueOwnedFill` | 220–243 |
| `FlushOwnedFills` | 245–346 |

`FillLockedBufs` (`160–196`) sits between `RangesOverlap` and `FillOwnedRange` and belongs to remesh. `FillOwnedRange:202` keeps calling it. `FlushOwnedFills:327` keeps calling `RenderPrepBufs`.

Signatures stay, so these call sites stay:

| Call | Line |
| --- | --- |
| `~CMapRenderChunk` → `CancelOwnedFill` | `MapRenderChunk.cpp:786` |
| `UpdatePools` → `ClearOwnedFills` / `FlushOwnedFills` | `MapRenderChunk.cpp:809–811` |
| `RenderSetup` → `FlushOwnedFills` | `MapRenderChunk.cpp:911` |
| `RenderPrep` → `QueueOwnedFill` | `MapRenderChunk.cpp:999` |

### Visibility scratch — `MapChunk.cpp`

| Function | Lines |
| --- | --- |
| `s_visQueue` | 388 |
| `DropQueuedVisibility` | 390–402 |
| `FlushQueuedVisibility` | 514–594 |

`jobs` is allocated at `524`. `liquids` is allocated at `549`. `VisRequest` (`342–346`), `VisJob` (`359–368`), `LiquidOut` (`370–374`), `VisBatch` (`376–385`), `CaptureVisibilitySnapshot` (`404–413`), and `EnqueueVisibility` (`415–425`) stay shaped so those two functions still compile.

Sort-bucket linking stays `LinkChunkBucket` (`427–434`) and `LinkLiquidBucket` (`436–450`). Frustum tests stay `RunChunkVisibility` (`452–506`).

Calls that stay: `CMapChunk::Purge` (`95`), `CMapRenderChunk::UpdatePools` (`806`), `PurgeChunkBufBlocks` (`671`), `CWorldScene::AddMapChunk` (`600`), `CMapChunk::CullAndBucketChunk` (`1122–1128`). Declaration stays `MapChunk.h:74`.

### Doodad-cull scratch — `World.cpp`

| Function | Lines |
| --- | --- |
| `DoodadCull` | 3899–3903 |
| `s_doodadCulls` | 3905 |
| `CullDoodadsExterior` | 3924–3991 |

`CullDoodadRange` (`3907–3920`) stays, including the frustum and occlusion tests. The barrier for a model that is not loaded yet stays at `3938–3943`. Declaration stays `World.h:258`.

## Remesh may edit

### `MapRenderChunk.cpp`

| Function | Lines |
| --- | --- |
| `FillLockedBufs` | 160–196 |
| `AllocBuf` | 596–666 |
| `UpdatePools` pool rebuild | 818–848 |
| `RenderPrepBufs` | 853–885 |
| `UseStreamingBufs` | 889–906 |
| `RenderSetup` buffer branch | 930–935 |
| `RenderPrep` LRU acquire | 992–997 |

Inside that rebuild: far-clip radius cap at `824–826`, index estimate `768 * count` at `831`, `s_bufList.LinkToTail` at `845`. The LRU touch on acquire is `s_bufList.LinkToHead` at `997`.

`s_bufList`'s type stays `MapRenderChunk.cpp:88` and `MapRenderChunk.h:78`.

These lines in the same functions stay:

| Lines | Why they stay |
| --- | --- |
| `UpdatePools:806–812` | visibility flush and owned-fill flush |
| `RenderSetup:911–928` | `FlushOwnedFills`, world matrix, `SelectLights` |
| `RenderPrep:999–1008` | `QueueOwnedFill`, `CreateLayers`, `UpdateLoaded` |
| `RenderSinglePass:1021–1024` | stream lock for the debug line path |
| `s_streamVertexCount` / `s_streamIndexCount` | `64`, `67` |

`FillLockedBufs:177` writes the second chunk at vertex base 145. `FillLockedBufs:193` appends its indices after `m_batch.m_count`.

### `MapChunk.cpp`

| Function | Lines |
| --- | --- |
| `CreateIndices(char*, int32_t)` | 302–338 |
| `CreateIndices(uint16_t*, CGxBatch*)` | 882–902 |

The hole-mask test stays at `309`. Declarations stay `MapChunk.h:137–138`.

### `DetailDoodad.cpp`

| Function | Lines |
| --- | --- |
| `AcquireBuffers` | 117–134 |
| `FillVertices` | 340–429 |
| `FillVerticesOnMain` | 626–640 |
| `QueueBatchFill` | 657–718 (pool miss at `666–671`) |
| `CDetailDoodadInstBatch::Draw` | 873–915 (pool miss at `884–893`) |

`CDetailDoodad::UpdatePools` (`542–582`) stays, including the 512-slot pools.

## What each merge keeps

- **Scratch onto terrain.** `MapRenderChunk.cpp:2443–2656` comes from terrain. Owned-fill, `FlushQueuedVisibility`, `DropQueuedVisibility`, and `CullDoodadsExterior` come from scratch.
- **Remesh onto that.** Buffer, index, and grass functions come from remesh. The multipass block and the scratch functions stay.

`FillLockedBufs` (`160–196`) is the interleaved function. Take remesh's body. Leave `RangesOverlap` above it and `FillOwnedRange` below it from scratch.
