# Remesh merge order

Campaign 4. Group D lands terrain (campaign 3) first; this squad starts after that branch closes. Scratch (campaign 5) is group C and shares the two files below. Take hunks in the order in this file. Each later hunk keeps the earlier owners' functions byte-for-byte.

## Owners

| Squad | Functions | Lines |
| --- | --- | --- |
| Terrain | `QueueMultiPass`, `FlushMultiPass`, `TerrainCapture`, `s_terrainBatch` | `WoW/Source/WorldClient/MapRenderChunk.cpp:2443`, `:2461`, `:2526`, `:2559` |
| Terrain | Declarations | `WoW/Source/WorldClient/MapRenderChunk.h:101` |
| Terrain | Call sites in `RenderChunksSinglePass` | `WoW/Source/WorldClient/World.cpp:3711`, `:3728` |
| Scratch | `FlushQueuedVisibility` | `WoW/Source/WorldClient/MapChunk.cpp:514` |
| Scratch | `DropQueuedVisibility` | `WoW/Source/WorldClient/MapChunk.cpp:390` |
| Scratch | `CancelOwnedFill` | `WoW/Source/WorldClient/MapRenderChunk.cpp:206` |
| Remesh | `AcquireBuffers` | `WoW/Source/WorldClient/DetailDoodad.cpp:117` |
| Remesh | `UseStreamingBufs` | `WoW/Source/WorldClient/MapRenderChunk.cpp:889` |
| Remesh | `AllocBuf` miss returns | `WoW/Source/WorldClient/MapRenderChunk.cpp:600`, `:635` |
| Remesh | `CreateIndices` (both overloads) | `WoW/Source/WorldClient/MapChunk.cpp:302`, `:882` |

`DetailDoodad.cpp` is only this squad. `World.cpp` has no remesh hunk: terrain keeps `:3711` and `:3728`, scratch keeps `s_doodadCulls` at `WoW/Source/WorldClient/World.cpp:3905`.

## 1. Terrain

Accept the terrain diff before any remesh edit to `MapRenderChunk.cpp`.

- `TerrainCapture` through `FlushMultiPass`: `WoW/Source/WorldClient/MapRenderChunk.cpp:2443-2656`.
- Header pair: `WoW/Source/WorldClient/MapRenderChunk.h:101-102`.
- `World.cpp:3701` calls `RenderSetup`, then `:3711` queues and `:3728` flushes. Those three calls stay in that order.

The batch path reads buffers this squad will retarget. After terrain has landed, these lines are the contract:

- Pool gate: `WoW/Source/WorldClient/MapRenderChunk.cpp:2537`. A chunk stays in the batch only when `m_chunkBuf` vertex and index exist, both pools are `s_gxVertexPool` / `s_gxIndexPool`, the vertex index is aligned, and `m_itemSize` on the index is 2. Failure calls `RenderMultiPassAlpha` (`:2538`), which draws whatever `RenderSetup` bound (`:2708`).
- Range copy: `:2553-2555` stores `m_batch.m_count`, `m_index / 2 + m_batch.m_start`, and `m_index / m_itemSize`.
- Rebind on flush: `:2622-2623` and `:2646-2647` bind `m_chunkBuf->m_vertexBuf` and `m_indexBuf` again. `DrawTerrainChunk` does the same at `:2521-2522`.

## 2. Scratch

Accept scratch's function bodies next. They sit in the same files as remesh and do not read index bytes.

- `DropQueuedVisibility`: `WoW/Source/WorldClient/MapChunk.cpp:390-402`.
- Visibility types and `s_visQueue`: `WoW/Source/WorldClient/MapChunk.cpp:340-388`.
- `FlushQueuedVisibility`: `WoW/Source/WorldClient/MapChunk.cpp:514-594`.
- `CancelOwnedFill`: `WoW/Source/WorldClient/MapRenderChunk.cpp:206-214`.

Remesh keeps these call sites, including their order:

- `PurgeChunkBufBlocks` calls `FlushQueuedVisibility` before it walks buffers: `WoW/Source/WorldClient/MapRenderChunk.cpp:671`.
- `UpdatePools` calls it before the owned-fill branch: `:806`, then `:808-812`.
- The destructor calls `CancelOwnedFill` and then `FreeBuf`: `:786-787`.

A later LRU steal may call `CancelOwnedFill`. The callee stays scratch's body.

## 3. Remesh

Three slices, in this order. Grass is a separate file and can land while terrain is still open. The `MapRenderChunk.cpp` slices wait until sections 1 and 2 are in the tree. Shared indices are last because they are the edit that has to satisfy the pool gate at `:2537`.

### 3a. `AcquireBuffers`

`WoW/Source/WorldClient/DetailDoodad.cpp:117-134`. The short-pool path already stores null (`:130-131`). Callers already return when either pointer is null:

- `QueueBatchFill`: `:666-671`.
- `Draw`: `:884-889`, before `FillVerticesOnMain` at `:892-893`.

The miss stays a null pair. No stream lock on that path. `FillVertices` (`:340`) is unchanged.

### 3b. `AllocBuf` miss, then `UseStreamingBufs`

Miss policy only. The hit path in `AllocBuf` (`WoW/Source/WorldClient/MapRenderChunk.cpp:596-666`) stays: flags `0x3` take `s_chunkBufBlockFreeList`, otherwise `s_renderChunkBufFreeList` then a block split.

Today both empty free lists return null (`:600-602`, `:635-637`). `RenderPrep` assigns that result at `:993`. `RenderSetup` then streams when `m_chunkBuf` is null (`:933-935` into `:889-906`).

Replace those two null returns with a steal of the `s_bufList` tail. The list is already an LRU: `UpdatePools` links every block at the tail (`:845`), `RenderPrep` moves the used block to the head (`:997`), `DestroyBufs` unlinks `m_bufLink` (`:411`). Head stays newest. Steal the tail. On the victim, call `CancelOwnedFill` (`:206`) and `FreeBuf` (`:761`) so the chunk drops its pointer before the block is reused.

After the steal exists, `RenderSetup` (`:930-935`) binds `m_chunkBuf` and does not call `UseStreamingBufs`. `UseStreamingBufs` (`:889-906`) remains in the file. The debug line path keeps its own `BufStream` pair: `RenderSinglePass` `:1021-1023`. That path does not go through `UseStreamingBufs`.

`s_streamVertexCount` / `s_streamIndexCount` stay `:64` and `:67`.

### 3c. `CreateIndices` sharing

Last. Hole-free chunks (`m_header->m_holes == 0`) share one static index buffer. The hole test stays `WoW/Source/WorldClient/MapChunk.cpp:309`. A non-zero mask still takes the private write in `:302-338`.

The `uint16_t*` overload (`:882-902`) still stamps the batch: `m_count`, `m_minIndex`, `m_maxIndex`. `QueueMultiPass` copies those at `WoW/Source/WorldClient/MapRenderChunk.cpp:2553-2554`. Base vertex for the second half is `m_maxIndex + 1` (`MapChunk.cpp:883`). `FillLockedBufs` still places the second chunk's vertices at +145 (`MapRenderChunk.cpp:177`) and still calls `CreateIndices` at `:189` and `:193`. A full hole-free cell is 768 `uint16`s (`8 * 8 * 12`); a dual chunk is the second run at that +145 base. `UpdatePools` may shrink only `s_pnEstimateIndex` (`:831`). `s_pnEstimateVertex` (`:830`) and the far-clip cap (`:821-826`) stay.

The shared `CGxBuf` has to pass the gate already landed in section 1:

- `m_pool == s_gxIndexPool`, `m_itemSize == 2`, `m_index` aligned (`MapRenderChunk.cpp:2537`).
- `m_chunkBuf->m_indexBuf` is that buffer, because flush binds it at `:2622-2623` and `:2646-2647`.
- `m_batch.m_start` plus `m_index / 2` is the shared range (`:2554`). Hole-free single chunks share the 768-index run. The dual-chunk run starts at the +145 vertex base.

`DestroyBufs` (`:415-437`) and `PurgeChunkBufBlocks` (`:680-695`) walk every chunk's index pointer and `BufDestroy` it. The shared buffer is destroyed once. The stream identity checks at `:422`, `:432`, `:684`, and `:690` are the existing pattern for "this pointer is not a private pool buf."

Declarations: `CreateIndices` stays `WoW/Source/WorldClient/MapChunk.h:137-138`. `FlushQueuedVisibility` stays `:74`. `AllocBuf` stays `WoW/Source/WorldClient/MapRenderChunk.h:95`. `UseStreamingBufs` stays `:157`. No new fields on `CMapRenderChunk` (`m_batch` is `:144` in the header) or `CMapChunk`.

## After the three hunks

Outdoor camera, steady frame:

- `RenderSetup` takes `:930-932` and does not enter `:934`.
- Hole-free chunks stay inside `QueueMultiPass` (the `:2537` branch is false) and flush with the same `m_batch.m_count` as before the share.
- Holed chunks still run `CreateIndices` at `MapChunk.cpp:309`.
- `~CMapRenderChunk` still calls `CancelOwnedFill` before `FreeBuf` (`MapRenderChunk.cpp:786-787`).
- Grass with `s_freeGxBufs.Count() < 2` returns at `DetailDoodad.cpp:670` and `:888` and does not call `FillVertices`.
