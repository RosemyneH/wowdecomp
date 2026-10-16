# Campaign 4 layout — stop remeshing terrain and grass

Names the records for the three slices after counters: grass misses do not stream, a terrain miss steals an LRU owned buffer, and hole-free chunks share one static index buffer. No new per-chunk fields. `CMapChunkBuf` / `CMapChunkBufBlock` offsets stay as in `MapRenderChunk.h:28-48`.

Steady outdoor camera inside the current pools: `UseStreamingBufs` and grass `FillVertices` stay at zero. Index-pool bytes drop by the hole-free share. Seeds, the fade ramp, and the 220-yard clamp stay.

## Grass miss

A short pool skips that batch for the frame. Batches that already hold a slot keep the existing distance fade. Neither path locks the shared stream buffer.

### Current miss

`AcquireBuffers` (`DetailDoodad.cpp:117-134`) pops a vertex/index pair from `CDetailDoodad::s_freeGxBufs` when `Count() >= 2`. Otherwise it writes null. The comment at `DetailDoodad.cpp:129` is the rule: the stream buffer is wiped by the next lock, so a fallback refills every frame.

`QueueBatchFill` (`DetailDoodad.cpp:666-671`) and `Draw` (`DetailDoodad.cpp:884-890`) already return when the pair is null, but `Draw` still calls `FillVerticesOnMain` (`DetailDoodad.cpp:892-894`) whenever `m_hasData` is clear, and both sites re-enter `AcquireBuffers` when `m_pool->m_usage == GxPoolUsage_Stream`.

`FillVertices` (`DetailDoodad.cpp:340`) runs from `FillMappedRange` (`DetailDoodad.cpp:617`) and `FillVerticesOnMain` (`DetailDoodad.cpp:632`).

### Miss state

No new field. The miss is the existing null pair on `CDetailDoodadInstBatch`:

| Field | Owner | Lifetime | Capacity |
| --- | --- | --- | --- |
| `m_vertexBuf`, `m_indexBuf` | the batch (`DetailDoodad.h:98-99`) | null for this frame on a miss; a popped pair until `FreeBuffers` (`DetailDoodad.h:133-145`) or age reclaim | one pair from the pool below |
| `m_adds` | the batch | unchanged across the miss | unchanged |

`AcquireBuffers` failure leaves both pointers null and returns. It does not call `BufStream`. `QueueBatchFill` and `Draw` return before `BufLock`, `FillVertices`, and `FillIndices`. `PrepareGrassFills` is still called from both branches of `InitializeRenderState` (`DetailDoodad.cpp:763`, `DetailDoodad.cpp:787`); a null pair never enters `s_fills`.

Next frame retries the free list. `m_adds` is not rebuilt. `CreateDetailDoodads` runs only when `m_detailDoodadInst` is null (`MapChunk.cpp:1611-1613`).

### Skip and fade

One code path. There is no per-miss alpha and no second mesh.

- **Skip.** No pool pair means no draw and no fill for that frame, inside the solid disk and inside the fade band.
- **Fade.** Batches that hold a pair keep the ramp already in the constants. Shader: `m_fade` from `detailDoodadDist`, window starts at `0.85 * dist` (`DetailDoodad.cpp:317-321`). Fixed function: tex1 scale `1 / (dist * 0.15)` and translate `z = dist * -0.85` (`DetailDoodad.cpp:777-780`). Alpha ref stays `CWorld::detailDoodadAlphaRef` (`DetailDoodad.cpp:755`, default 128 at `World.cpp:1719`).

A miss in the fade band is the skip. It is not a streamed tuft with extra alpha. Do not retune `0.85` or `0.15`.

### Pool that supplies the pair

| Field | Owner | Lifetime | Capacity |
| --- | --- | --- | --- |
| `s_gxVertexPool`, `s_gxIndexPool` | `CDetailDoodad` | `UpdatePools` until `FreeGxBufMat` | `512 * sizeof(CGxVertexPNCT) * s_vtxBufItems` and the index twin (`DetailDoodad.cpp:568-570`) |
| `s_freeGxBufs` | `CDetailDoodad` | pairs returned by `FreeBuffers` | 512 pairs, 1024 pointers (`DetailDoodad.cpp:572-577`) |
| `s_vtxBufItems`, `s_idxBufItems` | `CDetailDoodad` | pool generation | `(World::s_detailDoodadDensity << 6)` capped at 4096 (`DetailDoodad.cpp:550-563`) |

Do not grow this pool and do not change `kGrassSlots` (512, `DetailDoodad.cpp:606`). Age reclaim stays `m_age > 10` free buffers, `m_age > 20` free the inst (`Map.cpp:1910-1916`). That is what refills `s_freeGxBufs`.

### Stays

- Seed: `CRndSeed` set to 0, then `m_cOffset.x | (m_cOffset.y << 16)` (`MapChunk.cpp:1365-1367`). Cell picks, `sfloat_` placement, rotation, and scale stay (`MapChunk.cpp:1386-1388`, `1498-1500`, `1588-1589`). Hole cells still skip placement (`MapChunk.cpp:1454`).
- Distance gate: `detailDoodadDist > m_distToCamera` (`MapChunk.cpp:1611`).
- 220-yard clamp: `World::SetDetailDoodadDist` (`World.cpp:1316-1317`). The same cap is `kGroundEffectDistMaxExtended` (`WorldParam_CVarSlider.h:15`). A miss does not widen it. Changing the distance still sets `s_bPoolsDirty` (`World.cpp:1320-1323`).

## Terrain miss — steal an LRU owned buffer

`RenderPrep` calls `AllocBuf` when `m_chunkBuf` is null (`MapRenderChunk.cpp:992-993`). `AllocBuf` returns null when `s_chunkBufBlockFreeList` is empty (`MapRenderChunk.cpp:600-602`) or, on the single-chunk path, when that free list is also empty (`MapRenderChunk.cpp:635-637`). `RenderSetup` then calls `UseStreamingBufs` (`MapRenderChunk.cpp:930-935`), which locks `BufStream` and refills (`MapRenderChunk.cpp:889-905`).

That null branch steals instead. `UseStreamingBufs` stays in the file (`OFFSET: 0x007D0420`) and is not the miss path.

### Order already on `s_bufList`

`CMapRenderChunk::s_bufList` (`MapRenderChunk.h:78`, `MapRenderChunk.cpp:88`) is a `TSExplicitList` of `CMapChunkBufBlock` through `m_bufLink`. `UpdatePools` links every block at the tail (`MapRenderChunk.cpp:845`). `RenderPrep` moves the owning block to the head (`MapRenderChunk.cpp:997`). Head is most recently prepared. Tail is the LRU block. Walk with `Tail()` / `Prev()` (`stpl.h:65`, `stpl.h:68`).

No second LRU list.

### `StealLruBuf(int32_t flags, CMapRenderChunk* requester)`

Static function next to `AllocBuf`. Called from `RenderPrep` only after `AllocBuf` returns null. No new buffer and no `PoolCreate`.

Size class is `flags & 0x3`, the same split `AllocBuf` uses. Dual chunks pass orientation 1 or 2 (`MapArea.cpp:122-136`). Single chunks pass 0 (`MapChunk.cpp:297`).

Walk from the tail toward the head. Skip a block whose `m_blockLink` is still linked (it is on `s_chunkBufBlockFreeList`). Skip a buf whose `m_renderChunk` is null or is `requester`.

| Request | Steal |
| --- | --- |
| `flags & 0x3` | `m_bufs[0]` when `m_unkFlags & 0x3` and it has a victim. Dual stores 290 verts and 1536 indices (`MapRenderChunk.cpp:616-618`). |
| flags `== 0` | a buf with `(m_unkFlags & 0x3) == 0`. If both singles on that block are owned, take `m_bufs[0]`. Do not steal a dual buf for a single request (290 vs 145). |

Victim:

1. `CancelOwnedFill(victim)` (`MapRenderChunk.cpp:206`).
2. `victim->m_chunkBuf = nullptr`. Do not call `FreeBuf` (`MapRenderChunk.cpp:761-776`). `FreeBuf` would push the slot back onto a free list.
3. Leave the block on `s_bufList`.

Hand the same `CMapChunkBuf` to the requester. `m_renderChunk = requester`, `requester->m_chunkBuf = buf`. Clear `m_hasData` on the vertex buf (same as `AllocBuf` at `MapRenderChunk.cpp:611-612` and `628-629`) so `QueueOwnedFill` refills once. Then `s_bufList.LinkToHead(block)` so the stolen block is MRU. Index binding is recomputed from the new owner's hole mask (next section), not copied as contents.

Steal runs in `RenderPrep`, before `FlushOwnedFills` (`MapRenderChunk.cpp:911`) and before the draw. The victim is not drawn this frame. The slot is the same dynamic-pool reuse `AllocBuf` already does.

If no block matches the size class, leave `m_chunkBuf` null. Do not call `UseStreamingBufs`.

### Draw when the steal finds nothing

A null `m_chunkBuf` must not fall through into a draw. The previous chunk's prim bind would still be current, and `DrawTerrainChunk` reads `m_chunkBuf` unconditionally (`MapRenderChunk.cpp:2521-2522`).

Skip the chunk for the frame in:

- `RenderSetup` null branch (`MapRenderChunk.cpp:933-935`) — bind nothing
- `QueueMultiPass` (`MapRenderChunk.cpp:2526-2528`) — do not push a capture
- `DrawTerrainChunk`, the `FlushMultiPass` binds (`MapRenderChunk.cpp:2622-2623`, `2646-2647`), and `RenderSolid` (`MapRenderChunk.cpp:959`)

### Vertex record (unchanged capacity)

| Field | Owner | Lifetime | Capacity |
| --- | --- | --- | --- |
| `m_vertexBuf` | `CMapChunkBuf` inside `s_chunkBlockArray` | pool generation; assignment is the steal / `FreeBuf` lifetime | 145 verts, or 290 when `m_unkFlags & 0x3` |
| `s_gxVertexPool` | `CMapRenderChunk` | `UpdatePools` until `Destroy` | `s_pnEstimateVertex * stride` (`MapRenderChunk.cpp:833-834`) |

`s_pnEstimateVertex = 145 * count` (`MapRenderChunk.cpp:830`). `count = (2 * radius * radius + 1) & ~1`, `radius` from far clip capped at 160 (`MapRenderChunk.cpp:821-828`). Stride stays PN vs PNC via `s_gxBufVertexFormat`. Do not retune the cap. The campaign's allowed eviction is this steal, not a new radius.

145-vertex topology stays. The second half of a dual chunk is still written at vertex 145 (`MapRenderChunk.cpp:177`). `s_maxVertexCount` is 145 (`MapRenderChunk.cpp:37`).

### Debug lines keep streaming

`RenderSinglePass` (`MapRenderChunk.cpp:1013-1066`) builds a `GxPrim_Lines` normal gizmo through `BufStream`. `CWorldScene::RenderChunksSinglePass` calls it when `CWorld::enables & 0x40000000` (`World.cpp:3719-3744`). That path does not take an LRU slot and does not call `UseStreamingBufs`.

## One static index buffer

`CreateIndices` writes the same 8×8 pattern, 768 `uint16`s, for every chunk with `m_header->m_holes == 0` (`MapChunk.cpp:302-337`). `UpdatePools` sizes the dynamic index pool at `768 * count` indices, `s_pnEstimateIndex * 2` bytes (`MapRenderChunk.cpp:831-835`). Hole-free chunks stop paying that.

The hole mask is `SMChunk::m_holes` (`MapChunk.h:178`), read as `m_header->m_holes`. Bits match `CMap::s_holeMask` (`Map.cpp:286`). A render chunk is hole-free only when every non-null half has mask 0. If either half is non-zero, the whole draw uses a private index buffer, because it is one indexed draw.

### Static buffer

| Field | Owner | Lifetime | Capacity |
| --- | --- | --- | --- |
| `s_holeFreeIndexPool` | `CMapRenderChunk` static | `UpdatePools` until `Destroy` | 3072 bytes, `GxPoolUsage_Static` (`Types.h:254`) |
| `s_holeFreeIndexBuf` | `CMapRenderChunk` static | one upload per pool generation | 1536 `uint16`, `m_itemSize == 2`, `m_index == 0` |

`PoolCreate` size is the GPU allocation (`CGxDevice.cpp:1676`, `CGxDeviceVulkan.cpp:1542-1546`). 3072 bytes is `1536 * sizeof(uint16_t)`.

Layout of the 1536 indices:

| Range | Base vertex | Count | `m_batch` when this is the whole draw |
| --- | --- | --- | --- |
| `[0, 768)` | 0 | 768 | single chunk: start 0, count 768, min 0, max 144 |
| `[768, 1536)` | 145 | 768 | dual, both halves hole-free: start 0, count 1536, min 0, max 289 |

Base 145 is the value `CreateIndices(uint16_t*)` already uses. `s_chunkBatch.m_maxIndex` is `s_maxVertexCount - 1` = 144 (`MapRenderChunk.cpp:362`, `MapRenderChunk.cpp:37`). After the first half, `maxIndex` is 144, so the second half's base is `m_maxIndex + 1` = 145 (`MapChunk.cpp:883-884`). The uint16 values bake that base in. The draw's vertex offset stays 0 on top of them. `s_chunkBatch.m_count` is 768 (`MapRenderChunk.cpp:40`, `MapRenderChunk.cpp:366`).

Fill the buffer once under `BufLock` by the hole-free body of `CMapChunk::CreateIndices` (`MapChunk.cpp:306-337`): the `m_holes` test does not `continue`. Per cell, indices are `C,TL,BL, C,TR,TL, C,BR,TR, C,BL,BR` with

```
TL = base + col
TR = base + col + 1
C  = base + col + 9
BL = base + col + 17
BR = base + col + 18
```

and `base += 17` per row, 8×8. Call that twice, base 0 then base 145. Do not write a second triangulation. Set `m_hasData = 1` and do not lock it again until the pool is destroyed.

Do not store this pointer in `CMapChunkBuf::m_indexBuf`. `DestroyBufs` `BufDestroy`s every `m_indexBuf` (`MapRenderChunk.cpp:429-434`). A shared pointer would be destroyed once per slot.

### Resolve

```
CGxBuf* IndexBuf() const
```

on `CMapRenderChunk`: if `m_chunkBuf->m_indexBuf` is set, that private buf; else if the render chunk is hole-free, `s_holeFreeIndexBuf`; else null.

Bind and capture through `IndexBuf()`, not a raw `m_indexBuf`:

- `RenderSetup` (`MapRenderChunk.cpp:930-932`)
- `QueueMultiPass` (`MapRenderChunk.cpp:2527-2528`, `2553-2555`)
- `FlushMultiPass` (`MapRenderChunk.cpp:2622-2623`)
- shadow draw (`MapRenderChunk.cpp:2646-2647`)
- `DrawTerrainChunk` (`MapRenderChunk.cpp:2521-2522`)

Hole-free `m_batch` is set when the vertex buf is filled, without an index lock. Single: count 768, max 144. Dual hole-free: count 1536, max 289. `FillLockedBufs` (`MapRenderChunk.cpp:181-194`) skips `CreateIndices` when hole-free. `QueueOwnedFill` returns today when `m_indexBuf` is null (`MapRenderChunk.cpp:228-230`) and treats both bufs as ready only when `m_hasData` is set (`MapRenderChunk.cpp:232`). Hole-free must get past that null check and lock the vertex buf only. `FlushOwnedFills` has the same pair check (`MapRenderChunk.cpp:272-277`).

### Private indices

| Field | Owner | Lifetime | Capacity |
| --- | --- | --- | --- |
| `m_indexBuf` | `CMapChunkBuf`, non-null only while that slot is assigned to a render chunk with a non-zero hole mask | returned to `s_privateIndexFree` on `FreeBuf`, on steal onto a hole-free owner, or on pool destroy | 768 `uint16` (single) or 1536 `uint16` (dual) |
| `s_gxIndexPool` | `CMapRenderChunk` | created on the first holed assignment; `Destroy` / dirty `UpdatePools` drops it | high water of private bytes, see below |
| `s_privateIndexFree` | `CMapRenderChunk` static `TSGrowableArray<CGxBuf*>` | pool generation | free private bufs inside `s_gxIndexPool` |
| `s_privateIndexUsed` | `CMapRenderChunk` static | byte bump for new private bufs; free-list reuse does not bump | bytes |

`AllocBuf` stops creating index bufs at `MapRenderChunk.cpp:618` and `MapRenderChunk.cpp:654`. Vertex `BufCreate` stays. After a slot is assigned (fresh or stolen), bind indices from the new owner's mask:

- Hole-free: `m_indexBuf = nullptr`. Draw uses the static buf.
- Non-zero mask: pop `s_privateIndexFree`, or bump-allocate. Clear `m_hasData` and let `FillLockedBufs` call `CreateIndices` once. Winding stays that function.

A steal that crosses mask class does not keep the old index bytes. Same size class (the steal rule) means 768 stays 768 and 1536 stays 1536. Holed-to-holed may reuse the private buf, with `m_hasData` cleared, because the mask differs. Holed-to-hole-free pushes it onto `s_privateIndexFree`.

`UpdatePools` still creates the vertex pool at the current size. It creates the static index pool at 3072. It does not create `s_gxIndexPool` at `768 * count * 2`.

Grow `s_gxIndexPool` only when a new private buf does not fit (`s_privateIndexUsed + bytes > m_size`, or the pool is null). New size is `max(old * 2, used + need)`, starting from the one buf's byte size. Ceiling is today's pool: `768 * count * 2` (`MapRenderChunk.cpp:835`). Do not set `s_bPoolsDirty` for this grow. That flag destroys the vertex pool (`MapRenderChunk.cpp:808-819`, `377-392`). On grow, `BufDestroy` the private index views, `PoolDestroy` / `PoolCreate` the index pool only, `BufCreate` them back at the same byte offsets, clear their `m_hasData`, and let the owned fill rewrite them. Vertex `m_hasData` stays. A steady camera that already fits does not grow.

If the ceiling is hit, that holed chunk skips the frame the same way a failed steal skips. It does not stream. A fully holed view fits at the ceiling, which is the old pool. Hole-free slots add 0 bytes, so `s_gxIndexPool->m_size` drops by that share. The static 3072 bytes sit in the other pool.

Packed offsets replace `1536 * (m_vertexCount + i)` (`MapRenderChunk.cpp:654`). That stride reserved a slot per block, including hole-free ones.

### `DrawTerrainBatch` shares one index buffer

`DrawTerrainBatch` binds one `m_primIndexBuf` and uses per-draw `firstIndex` (`CGxDeviceVulkan.cpp:1502-1529`). `QueueMultiPass` stores `firstIndex = index->m_index / 2 + m_batch.m_start` (`MapRenderChunk.cpp:2553-2555`). `FlushMultiPass` groups by texture and fog, then binds the key chunk's index buf (`MapRenderChunk.cpp:2596-2623`).

Hole-free `firstIndex` is 0 for every such chunk (`m_index == 0`, `m_start == 0`). They can share one bind of `s_holeFreeIndexBuf`. Private bufs use offsets inside `s_gxIndexPool` and can share that bind with each other. A group must not mix the two pools.

Widen the batch gate at `MapRenderChunk.cpp:2537`: accept `s_holeFreeIndexPool` as well as `s_gxIndexPool`. Add the index-pool identity to the texture/fog match in `FlushMultiPass` (`MapRenderChunk.cpp:2596`). D3D's `DrawTerrainBatch` returns 0 (`CGxDevice.cpp:146-149`) and already falls back to `DrawTerrainChunk`; that fallback binds `IndexBuf()` per chunk.

## What a steady frame does

Inside the current grass pool and terrain block count, with a camera that does not cross the pool edge:

- Grass batches that have pairs keep `m_hasData`. `FillVertices` does not run.
- Grass batches without pairs return. They do not stream.
- Terrain chunks that have `m_chunkBuf` take the `LinkToHead` path. `AllocBuf` and `StealLruBuf` do not run.
- A terrain chunk that crosses the edge steals one owned vertex buf and refills it once. It does not call `UseStreamingBufs`.
- Hole-free index bytes stay the one static upload. Private index locks run only when a holed slot is new or stolen.

`// OFFSET:` placement, hole tests, cull thresholds, `VkDrawUniforms`, and `GpuDrawItem` are untouched. The miss call in `RenderSetup` is the change. `UseStreamingBufs` itself is not rewritten.
