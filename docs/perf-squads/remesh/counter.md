# Campaign 4 — Remesh counters

Instrumentation only. Five fields, two clock zones. No mesh, seed, hole, fade, or pool-radius change.

Append the fields to the Campaign 0 one-second console line. Guard every read and add with that campaign’s compile-out flag, default off. Reset the addends when the line is printed. `holefree_priv768` is recomputed at print time.

Clocks are `OsGetAsyncTimeClocks` (`CLOCK_MONOTONIC` on Linux, `Engine/Source/Os/Linux/TimeManager.cpp:59`). Store the raw delta. The accountant divides by `OsGetAsyncClocksPerSecond`.

`FillVertices` runs inside `Job::ParallelFor`, so its count and clocks are `std::atomic` relaxed adds. The other three event counters run on the main thread; use the same atomics. The gauge walk runs on the main thread while the line is built.

A steady outdoor camera inside the current pools prints `grass_fill`, `grass_acquire_miss`, `terrain_stream`, and `terrain_alloc_miss` at **0**. `holefree_priv768` stays at the hole-free population until the shared index buffer lands.

## Printed fields

| Field | Window | Unit |
| --- | --- | --- |
| `grass_fill` | sum since last line | calls of `CDetailDoodadInstBatch::FillVertices` |
| `grass_fill_clk` | sum since last line | thread-clocks inside that function (workers overlap) |
| `grass_acquire_miss` | sum since last line | `AcquireBuffers` entries that leave both pointers null |
| `terrain_stream` | sum since last line | calls of `CMapRenderChunk::UseStreamingBufs` |
| `terrain_stream_clk` | sum since last line | main-thread clocks inside that function |
| `terrain_alloc_miss` | sum since last line | `CMapRenderChunk::AllocBuf` returns of null |
| `holefree_priv768` | value at print | hole-free single chunks that own a private 768-index buffer |

Divide the four sums by the frame count already on the Campaign 0 line for the per-frame mean. A sum of 0 is a per-frame 0.

## `grass_fill`

Increment once on entry to `CDetailDoodadInstBatch::FillVertices` (`WoW/Source/WorldClient/DetailDoodad.cpp:340`). Take the clock there and add the delta on return. Both callers reach this function:

- `FillMappedRange` at `DetailDoodad.cpp:617`, from `Job::ParallelFor` at `DetailDoodad.cpp:732`.
- `FillVerticesOnMain` at `DetailDoodad.cpp:632`, from `CDetailDoodadInstBatch::Draw` at `DetailDoodad.cpp:893` when `m_hasData` is clear or `m_valid` is clear (`DetailDoodad.cpp:892`).

A pool hit clears `m_hasData` on both buffers (`DetailDoodad.cpp:124`). The next fill writes the batch once and sets `m_hasData` (`DetailDoodad.cpp:638` on the draw path, `DetailDoodad.cpp:740` on the mapped path). Warm slots then stay out of `FillVertices`.

The null branch of `AcquireBuffers` (`DetailDoodad.cpp:130`) leaves the batch unfilled: `QueueBatchFill` returns at `DetailDoodad.cpp:670`, and `Draw` returns at `DetailDoodad.cpp:888`. Those batches show up in `grass_acquire_miss` and stay out of `grass_fill`.

## `grass_acquire_miss`

Increment on the short-pool branch of `AcquireBuffers` (`DetailDoodad.cpp:117`), where `s_freeGxBufs.Count() < 2` (`DetailDoodad.cpp:120`) and both out-pointers are set null (`DetailDoodad.cpp:130`). One add per call.

Callers, each able to miss in the same frame:

- `QueueBatchFill` at `DetailDoodad.cpp:667`, from `PrepareGrassFills`.
- `Draw` at `DetailDoodad.cpp:885`.

The hit path (`DetailDoodad.cpp:121`) is a pool take. It stays out of this counter. The 512-slot pools are created at `DetailDoodad.cpp:569`.

## `terrain_stream`

Increment on entry to `CMapRenderChunk::UseStreamingBufs` (`WoW/Source/WorldClient/MapRenderChunk.cpp:889`). Clock the whole function, including `BufStream` (`MapRenderChunk.cpp:899`) and `RenderPrepBufs` (`MapRenderChunk.cpp:902`), which locks and calls `FillLockedBufs`.

The caller is `RenderSetup` (`MapRenderChunk.cpp:934`) when `m_chunkBuf` is null (`MapRenderChunk.cpp:930`).

`RenderSinglePass` (`MapRenderChunk.cpp:1021`) is the debug line stream. It stays off this counter.

## `terrain_alloc_miss`

Increment at each null return inside `CMapRenderChunk::AllocBuf` (`MapRenderChunk.cpp:596`):

- Dual chunk (`flags & 0x3`) and `s_chunkBufBlockFreeList` is empty: `MapRenderChunk.cpp:601`.
- Single chunk, `s_renderChunkBufFreeList` is empty, and the block list is empty: `MapRenderChunk.cpp:636`.

The caller is `RenderPrep` (`MapRenderChunk.cpp:993`). A null leaves `m_chunkBuf` null, and `RenderSetup` then counts `terrain_stream` for that chunk. The two fields stay separate: a stream call with `terrain_alloc_miss` still 0 is a chunk that never received a pool buffer.

Returned buffers (`MapRenderChunk.cpp:613`, `MapRenderChunk.cpp:619`, `MapRenderChunk.cpp:630`, `MapRenderChunk.cpp:665`) stay out of this counter.

## `holefree_priv768`

Gauge, walked at line-build from `CMapRenderChunk::s_chunkBlockArray` (`MapRenderChunk.cpp:836`). Count a `CMapChunkBuf` when all of these hold:

- `m_renderChunk` is set and `m_indexBuf` is set.
- `m_indexBuf->m_itemCount == 768` and `m_indexBuf->m_pool == CMapRenderChunk::s_gxIndexPool`. That is the private buffer from `BufCreate` at `MapRenderChunk.cpp:654` (`GxPoolUsage_Dynamic`, pool created at `MapRenderChunk.cpp:835`). `m_itemCount` is `CGxBuf::m_itemCount`.
- `m_renderChunk->m_mapChunks[1] == nullptr`. Single chunks are `AddBatch(this, nullptr, …, 0)` at `WoW/Source/WorldClient/MapChunk.cpp:297` and `MapChunk.cpp:961`.
- `m_mapChunks[0]->m_header->m_holes == 0`.

`CMapChunk::CreateIndices` (`MapChunk.cpp:302`) walks the same 8×8 grid. The hole test is `m_header->m_holes & CMap::s_holeMask[holeMaskIndex]` at `MapChunk.cpp:309`. A zero mask emits every cell, 12 indices each (`MapChunk.cpp:318`), 64 × 12 = 768 `uint16`s. A non-zero mask still sits in a 768-capacity buffer and stays out of this gauge: those chunks keep private indices.

Bytes in the gauge are `holefree_priv768 * 768 * sizeof(uint16_t)`. `UpdatePools` sizes the whole index pool at `768 * count` indices (`MapRenderChunk.cpp:831`), with `count` from the far-clip radius capped at 160 (`MapRenderChunk.cpp:824`). The gauge is the hole-free share of that pool that is actually assigned.

A hole-free chunk that missed `AllocBuf` has a null `m_chunkBuf` and is absent here. It is already on `terrain_alloc_miss` and `terrain_stream`. The owned fill sets index `m_hasData` at `MapRenderChunk.cpp:343` (`RenderPrepBufs` does the same at `MapRenderChunk.cpp:883`). `QueueOwnedFill` then leaves that chunk alone (`MapRenderChunk.cpp:232`). The gauge still counts the buffer.

Dual chunks (`m_unkFlags & 0x3`) take the 1536-index buffer at `MapRenderChunk.cpp:618`. Their second copy uses base vertex 145: `s_chunkBatch.m_maxIndex` is `s_maxVertexCount - 1` (`MapRenderChunk.cpp:362`, `s_maxVertexCount` is 145 at `MapRenderChunk.cpp:37`), and `CreateIndices` sets `baseVertex = m_maxIndex + 1` (`MapChunk.cpp:883`). `FillLockedBufs` writes that second mesh at vertex offset 145 (`MapRenderChunk.cpp:177`) and index offset `m_batch.m_count` (`MapRenderChunk.cpp:193`). A 1536 buffer stays out of `holefree_priv768`.

## Line

```
grass_fill=<n> grass_fill_clk=<clocks> grass_acquire_miss=<n> terrain_stream=<n> terrain_stream_clk=<clocks> terrain_alloc_miss=<n> holefree_priv768=<n>
```

Field names stay frozen for the rest of the campaign.
