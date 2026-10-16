# Campaign 3 leak review

Persistent scratch may keep its allocation across frames. It must not append without a ceiling, and it must not still name a `CMapRenderChunk` after that chunk or its buffers are torn down.

## `s_terrainBatch` lifetime

`s_terrainBatch` is a process-lifetime vector at `WoW/Source/WorldClient/MapRenderChunk.cpp:2461`. Each entry is a `TerrainCapture` (`MapRenderChunk.cpp:2443`) whose first field is a raw `CMapRenderChunk*` (`MapRenderChunk.cpp:2444`).

`QueueMultiPass` writes that pointer at `MapRenderChunk.cpp:2544` and `push_back`s at `MapRenderChunk.cpp:2556`. The only caller is the per-bucket loop in `CWorldScene::RenderChunksSinglePass` (`WoW/Source/WorldClient/World.cpp:3711`). `FlushMultiPass` is the matching retire (`World.cpp:3728`). It returns immediately when the vector is empty (`MapRenderChunk.cpp:2560`) and otherwise `clear()`s at `MapRenderChunk.cpp:2655`, after the layer groups and the shadow pass.

`std::vector::clear` drops `size` and leaves `capacity` in place. That is the retain Campaign 3 asks for. Nothing calls `shrink_to_fit` or swaps in an empty vector, so this vector does not give its heap block back every frame.

The pointer is still live for the whole flush. Layer pairing reads `cap.chunk` from `MapRenderChunk.cpp:2567` through `MapRenderChunk.cpp:2630`, and the shadow pass reads it again at `MapRenderChunk.cpp:2637`–`2652` (`m_shadowTexture`, `m_chunkBuf`, `m_batch`). `clear()` at `MapRenderChunk.cpp:2655` is the first point where the vector no longer holds those pointers.

`used` and `draws` are not persistent. `FlushMultiPass` builds them on the stack at `MapRenderChunk.cpp:2572` and `MapRenderChunk.cpp:2573`, so their capacity dies with the flush. `draws.clear()` at `MapRenderChunk.cpp:2585` only reuses the local buffer inside one flush. Promoting them next to `s_terrainBatch` is the slice; they then follow the same rules as that vector.

## Bound

One flush is one `(pass, layers)` bucket (`World.cpp:3691`–`3728`). A capture is pushed only when the chunk’s vertex and index buffers sit in `s_gxVertexPool` / `s_gxIndexPool` (`MapRenderChunk.cpp:2537`). Those pools are sized from `count` in `CMapRenderChunk::UpdatePools` (`MapRenderChunk.cpp:828`), and `radius` is capped at 160 (`MapRenderChunk.cpp:824`). Peak `count` is 51200 slots. A flush that reaches `clear()` therefore tops out at that many captures (about 468 bytes each, on the order of 24 MB) plus a `used` byte and a `GxTerrainDraw` per grouped draw.

That ceiling holds only when every exit clears. A second `push_back` without `clear()` appends another bucket onto the same vector. Repeating that every frame is unbounded. `reserve` may rise once to the current pool slot count. It must not climb by the batch size every frame, and `push_back` must not run once `size` is already at that count.

## Chunk teardown

No destroy path removes entries from `s_terrainBatch`.

| Path | What it does | Batch |
| --- | --- | --- |
| `CMapRenderChunk::~CMapRenderChunk` `MapRenderChunk.cpp:785` | `FreeBuf` (`MapRenderChunk.cpp:787`, body `761`) nulls `m_chunkBuf` at `MapRenderChunk.cpp:775`. `FreeLayers` (`MapRenderChunk.cpp:467`) zeros `m_layersCount` and closes layer and shadow textures. | Pointer left in the vector. |
| `CMap::FreeRenderChunk` `WoW/Source/WorldClient/Map.cpp:3224` | Calls that destructor at `Map.cpp:3226`, then links the object onto `s_mapRenderChunkFreeList` at `Map.cpp:3227`. `AllocRenderChunk` placement-news the same address at `Map.cpp:3217`. | Stale pointer aliases the free-list node or the next chunk. |
| `CMapChunk::Purge` `WoW/Source/WorldClient/MapChunk.cpp:94` | Calls `FreeRenderChunk` at `MapChunk.cpp:98`. `CMapArea::Purge` reaches it at `WoW/Source/WorldClient/MapArea.cpp:61`. | Same dangling pointer, on a live unload. |
| `CMap::ClearAllRenderChunks` `Map.cpp:469` | `FreeBuf` on every render chunk (`Map.cpp:472`). | Chunk objects remain; `m_chunkBuf` is null. Flush then loads `cap.chunk->m_chunkBuf->m_vertexBuf` at `MapRenderChunk.cpp:2622`. |
| `CMap::ProcessRenderChunkUpdateList` `Map.cpp:1926` | `FreeBuf` when `m_lastUpdateTime` passes 2 seconds (`Map.cpp:1933`). | Same null `m_chunkBuf` under a still-stored pointer. |
| `CMapRenderChunk::Destroy` `MapRenderChunk.cpp:378` | `DestroyBufs` (`MapRenderChunk.cpp:397`) `BufDestroy`s each vertex and index buffer (`MapRenderChunk.cpp:423`, `MapRenderChunk.cpp:433`), then `PoolDestroy`s both pools (`MapRenderChunk.cpp:385`, `MapRenderChunk.cpp:390`). | Captures still name chunks whose `CGxBuf`s are gone. |
| `CMapRenderChunk::UpdatePools` `MapRenderChunk.cpp:805` | On a dirty pool: `ClearAllRenderChunks` (`MapRenderChunk.cpp:818`) then `Destroy` (`MapRenderChunk.cpp:819`). | Same pair of holes. |
| `CMapChunk::Destroy` `MapChunk.cpp:288` | Calls `CMapRenderChunk::Destroy`. `CMap::Destroy` calls that at `Map.cpp:525`. | Map unload with a non-empty batch. |

`CWorldScene::Render` calls `UpdatePools` at `WoW/Source/WorldClient/CWorldSceneRender.cpp:92`, before `RenderChunksSinglePass` queues anything. The batch is empty there only because the previous bucket reached `MapRenderChunk.cpp:2655`. The queue loop itself (`World.cpp:3699`–`3726`) does not purge. The hole is every other call to the table above while `size() > 0`.

`GxTerrainDraw` (`Engine/Source/Gx/CGxDevice/CGxDevice.h:72`) has no chunk pointer. `CGxDeviceVulkan::DrawTerrainBatch` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1502`) copies matrices and draw args into the frame’s draw-item and indirect rings (`CGxDeviceVulkan.cpp:1516`–`1538`) before it returns. `draws` is CPU scratch after that call. It must not gain a `CMapRenderChunk*`.

GPU buffers named by a capture are the chunk pools, not the vectors. `PoolDestroy` (`CGxDeviceVulkan.cpp:1581`) goes through `IReleasePool` (`CGxDeviceVulkan.cpp:1556`), which `RetireBuffer`s a non-borrowed alloc (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1165`) onto `VkFrame::garbage`. `Collect` (`VkContext.cpp:1198`) destroys that garbage only after `PrepareFrame` waits the frame fence (`VkContext.cpp:709`). That retire is valid when the pool dies at the next `UpdatePools`, with the batch already cleared. It is not valid while `s_terrainBatch` still points at those `CGxBuf`s, and `DestroyBufs` does not wait on its own.

## Required before the persistent-scratch slice lands

- `s_terrainBatch`, `used`, and `draws` stay allocated. Retire them with `clear()`. Do not `shrink_to_fit`, swap with a temporary, or assign a fresh vector each flush.
- Cap all three at the current pool slot count from `UpdatePools` (`MapRenderChunk.cpp:824`–`828`). One `reserve` up to that count is enough. A later larger pool (still ≤ 160 radius) may reserve once more. A smaller pool keeps the old capacity.
- `FlushMultiPass` still `clear()`s `s_terrainBatch` at `MapRenderChunk.cpp:2655`, after the shadow pass. `draws.clear()` stays on each group (`MapRenderChunk.cpp:2585`). The empty early return (`MapRenderChunk.cpp:2560`) must not be the only retire if a capture was pushed.
- `~CMapRenderChunk` (`MapRenderChunk.cpp:785`) drops every capture whose `chunk` is `this` before `FreeBuf` (`MapRenderChunk.cpp:787`). That covers `FreeRenderChunk` (`Map.cpp:3224`) and `CMapChunk::Purge` (`MapChunk.cpp:94`).
- `ClearAllRenderChunks` (`Map.cpp:469`) and `ProcessRenderChunkUpdateList` (`Map.cpp:1933`) `clear()` the batch before `FreeBuf`, or the destructor scrub above has already removed those pointers.
- `Destroy` / `DestroyBufs` (`MapRenderChunk.cpp:378`, `MapRenderChunk.cpp:397`) `clear()` the batch before `BufDestroy` and `PoolDestroy`. `UpdatePools` (`MapRenderChunk.cpp:818`) and `CMapChunk::Destroy` (`MapChunk.cpp:288`) then cannot free a buffer a capture still names.
- After `clear()`, `size` is 0. Do not read the leftover pointer bits in the spare capacity. Do not iterate to `capacity`.
