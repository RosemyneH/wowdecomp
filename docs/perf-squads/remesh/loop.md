# Campaign 4 — Loop

**NEXT SLICE.** Not closed. Two budget clauses already hold on a steady outdoor camera inside the current pools. The index pool does not.

No scene log and no miss counters are in the tree (Campaign 0 never landed). This is control flow, not a measured rate.

## Budget

| Clause | Call | Result |
| --- | --- | --- |
| Grass `FillVertices` stays at 0 | Steady camera, batch already holds a pool slot | Holds |
| `UseStreamingBufs` stays at 0 | Steady camera, chunk already holds a pool slot | Holds |
| Index pool bytes drop by the hole-free share | `PoolCreate` of `CMapRenderChunk_idx` | Open |

Default outdoor knobs used only to check that "inside the current pool" is the steady case: `farclip` `"350"` (`WorldParam_CVar.cpp:696`), `groundEffectDist` `"70.0"` (`WorldParam_CVar.cpp:712`), `groundEffectDensity` `"16"`. Do not retune them.

## Grass `FillVertices` — holds

A miss does not stream and does not fill.

- `AcquireBuffers` (`DetailDoodad.cpp:117-134`) takes two slots from `s_freeGxBufs` or stores null. No `BufStream`.
- `QueueBatchFill` (`DetailDoodad.cpp:670-672`) and `Draw` (`DetailDoodad.cpp:888-890`) return when either pointer is null. That batch is skipped for the frame.
- `FillVertices` runs only when a held buffer has `m_hasData` clear or `m_valid` clear: `QueueBatchFill` (`DetailDoodad.cpp:674-678`) into `FillMappedRange` (`DetailDoodad.cpp:617`), and `Draw` (`DetailDoodad.cpp:892-893`) into `FillVerticesOnMain` (`DetailDoodad.cpp:632`).

On a steady camera those flags stay set.

- Visible instances set `m_age = 0` every draw (`World.cpp:3586`). `FreeBuffers` runs only after `m_age > 10` (`Map.cpp:1910-1912`), which puts the pair back on `s_freeGxBufs` (`DetailDoodad.h:133-144`).
- `AcquireBuffers` clears `m_hasData` only when a slot is taken (`DetailDoodad.cpp:124-125`). A batch that already has a non-stream buffer does not take again (`DetailDoodad.cpp:666-668`, `DetailDoodad.cpp:884-886`).
- The grass pools are `GxPoolUsage_Dynamic` (`DetailDoodad.cpp:569-570`). Vulkan discards a pool only in `IReleasePools` (`CGxDeviceVulkan.cpp:1568-1572`), not per frame. D3D locks a dynamic pool with `D3DLOCK_NOOVERWRITE` and does not call `Discard` (`CGxDeviceD3d.cpp:154-156`).

The first frame a batch takes a slot still fills once. A camera that is already steady does not. The 512-slot pool (`DetailDoodad.cpp:568-577`) is what "inside the current pool" means; the 220-yard cap (`World.cpp:1316-1317`) stays as it is.

## `UseStreamingBufs` — holds

`RenderSetup` calls it only when `m_chunkBuf` is null (`MapRenderChunk.cpp:930-934`). `AllocBuf` returns null only when the free list is empty (`MapRenderChunk.cpp:600-601` dual, `MapRenderChunk.cpp:635-636` single).

A chunk that got a slot keeps it on a steady camera:

- `RenderPrep` allocates once, then `LinkToHead` on `s_bufList` (`MapRenderChunk.cpp:991-997`).
- `RenderSetup` zeros `m_lastUpdateTime` (`MapRenderChunk.cpp:912`). `ProcessRenderChunkUpdateList` frees only after that timer passes 2 seconds (`Map.cpp:1933-1934`).
- The pool is the far-clip disk, not the frustum: `radius = 1 - (int)(s_farClip * -0.03)`, cap 160, `count = (2 * radius * radius + 1) & ~1` (`MapRenderChunk.cpp:821-831`). At `farclip` 350 that is radius 11, `count` 242, which is larger than the visible set.

`s_bufList` is not an eviction list. The only writes are `LinkToTail` at pool create (`MapRenderChunk.cpp:845`) and `LinkToHead` on use (`MapRenderChunk.cpp:997`). Nothing steals the tail. That miss is a camera whose visible set does not fit. It is outside this budget line.

`RenderSinglePass` still `BufStream`s the debug line path (`MapRenderChunk.cpp:1021-1024`). Leave it.

## Index pool — open

`UpdatePools` still does `s_pnEstimateIndex = 768 * count` and `PoolCreate(..., s_pnEstimateIndex * 2, ...)` (`MapRenderChunk.cpp:831-835`). At `farclip` 350 that is `768 * 242 * 2` = 371,712 bytes, and none of it is conditional on holes.

Every slot gets a private index buf:

- Dual: 1536 `uint16`s (`MapRenderChunk.cpp:618`)
- Single: 768 `uint16`s (`MapRenderChunk.cpp:654`)

`FillLockedBufs` always calls `CreateIndices` for both halves (`MapRenderChunk.cpp:188-193`). `CreateIndices` writes the same 8×8, 12-index cell whenever the hole bit is clear, 768 indices for a mask of 0 (`MapChunk.cpp:302-337`). The dual half bakes base vertex `m_maxIndex + 1`, which is 145 (`MapChunk.cpp:882-884`, `s_maxVertexCount` 145 at `MapRenderChunk.cpp:37`). There is no static index buf.

Draws assume that private buf: `RenderSetup` (`MapRenderChunk.cpp:932`), `DrawTerrainChunk` (`MapRenderChunk.cpp:2522`), the multipass batch (`MapRenderChunk.cpp:2623`), and the shadow pass (`MapRenderChunk.cpp:2647`). `QueueMultiPass` also requires `index->m_pool == s_gxIndexPool` or the chunk leaves the batch (`MapRenderChunk.cpp:2537-2539`). `firstIndex` is `index->m_index / 2` (`MapRenderChunk.cpp:2554`). `baseVertex` there is the vertex-pool byte offset, not +145 (`MapRenderChunk.cpp:2555`).

## Next writer task

**Shared hole-free indices.** One concern. One slice.

- One static index buf for `m_header->m_holes == 0`: 768 `uint16`s, the pattern `CreateIndices` already writes (`MapChunk.cpp:318-330`).
- One static dual buf, 1536 `uint16`s, second half at base 145, same winding. Use it only when both halves have a zero mask. If either mask is non-zero, that render chunk keeps a private index buf and still calls `CreateIndices`.
- Both static bufs are `CGxBuf`s in `s_gxIndexPool`, so `QueueMultiPass` (`MapRenderChunk.cpp:2537`) still batches those chunks. `firstIndex` becomes the static buf's offset. Do not `BufLock` or `CreateIndices` the static buf per chunk, and do not `BufDestroy` it from `FreeBuf`.
- `PoolCreate` size for `CMapRenderChunk_idx` becomes `1536 * holedSingleSlots + 3072 * holedDuals + 1536 + 3072`. Hole-free slots drop out of the `768 * count` term. Count masks with the same `m_header->m_holes` test `CreateIndices` uses. Rebuild through the existing `s_bPoolsDirty` path when the loaded holed set no longer fits. Do not leave the create size at `768 * count * 2`.
- Do not change vertex pool size, far clip, the 160 cap, grass, `UseStreamingBufs`, `RenderSinglePass`, hole tests, or winding.

LRU steal on `s_bufList` is not this task. The steady in-pool camera already skips `UseStreamingBufs`.
