# Edges — Campaign 4 remesh

Holds for the grass miss, the terrain miss, hole-free index sharing, and the two distance clamps. A slice passes only when each hold below stays true. Hole masks, the 145-vertex grid, winding, grass seeds, the fade band, and both clamps stay as they are.

## Grass pool empty

**Hold.** A batch that cannot take a vertex/index pair leaves both pointers null and draws nothing. `FillVertices` does not run, and the batch does not take the shared stream buffer.

`AcquireBuffers` needs two free entries. With a count of 0 or 1 it writes both out-pointers null and ignores the requested sizes (`WoW/Source/WorldClient/DetailDoodad.cpp:117-134`). The comment on that path is the reason a stream fallback refills every frame: `BufStream` is one buffer per target and clears `m_hasData` on every call (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1062-1074`).

Both callers already stop on the null pair:

- `QueueBatchFill` returns before the lock (`DetailDoodad.cpp:666-671`).
- `Draw` returns before `FillVerticesOnMain` (`DetailDoodad.cpp:884-889`).

A successful take is the other path. It clears `m_hasData` on both buffers (`DetailDoodad.cpp:124-125`), and the later fill rebuilds instance matrices from `m_adds` (`DetailDoodad.cpp:340-353`). The empty path never reaches that fill.

The pool is 512 pairs, built once in `UpdatePools` (`DetailDoodad.cpp:568-577`). Detail distance does not grow it. Past 512 live batches, further batches hit this null path. An inst has four batches (`DetailDoodad.cpp:835-868`), so a chunk can draw the earlier batches and skip the later ones in the same frame.

`FreeBuffers` returns a buffer only when its pool is `s_gxVertexPool` (`WoW/Source/WorldClient/DetailDoodad.h:133-145`). A stream pointer assigned on the miss would be dropped on the next null-out and would not come back as a free slot.

Seeds stay on the inst. `CreateDetailDoodads` runs only when `m_detailDoodadInst` is null (`WoW/Source/WorldClient/MapChunk.cpp:1611-1613`), and the placement loop consumes `CRandom::sfloat_` (`MapChunk.cpp:1498-1500`). A miss leaves that inst in place.

**Pass.** With the free list at 0 or 1, the batch is absent for the frame, `s_freeGxBufs` is unchanged, and `FillVertices` is not called. The shader fade (`DetailDoodad.cpp:317-321`) and the fixed-function fade matrix (`DetailDoodad.cpp:777-780`) still apply to batches that did take a slot.

## Terrain pool empty

**Hold.** A terrain miss takes the least-recently used block on `s_bufList`. The chunk that loses the block does not draw that buffer, and `RenderSetup` does not refill it through `UseStreamingBufs`. `RenderSinglePass` still streams.

`AllocBuf` returns null in two places:

- Dual (`flags & 0x3`) when `s_chunkBufBlockFreeList` has no head (`WoW/Source/WorldClient/MapRenderChunk.cpp:597-601`). This path never looks at `s_renderChunkBufFreeList`, so a dual chunk can miss while single buffers are still free.
- Single when the single free list is empty and the block free list has no tail (`MapRenderChunk.cpp:622-636`).

`RenderPrep` then leaves `m_chunkBuf` null and skips the `s_bufList` touch and the owned fill (`MapRenderChunk.cpp:992-999`). `RenderSetup` calls `UseStreamingBufs` (`MapRenderChunk.cpp:930-934`), which locks the shared stream buffer and refills through `RenderPrepBufs` (`MapRenderChunk.cpp:889-902`). Dual chunks ask that stream for twice the single counts, 290 vertices and 1536 indices (`MapRenderChunk.cpp:894-897`). The next `BufStream` clears `m_hasData` (`CGxDevice.cpp:1073`), so the refill repeats every frame.

`s_bufList` is already recency order. `UpdatePools` links every block at the tail (`MapRenderChunk.cpp:845`). `RenderPrep` moves the block in use to the head (`MapRenderChunk.cpp:997`). The tail is the block to take. After the take:

- The victim's `m_chunkBuf` is null before its `RenderSetup`. `CancelOwnedFill` drops it if a fill is queued (`MapRenderChunk.cpp:206-214`). Otherwise the queued fill writes the victim's mesh into the new owner's buffer.
- A split block has two owners (`m_bufs[0]` and `m_bufs[1]`). Both lose `m_chunkBuf`.
- The new owner gets a buffer sized for its flags. Dual is 290 vertices and 1536 indices (`MapRenderChunk.cpp:617-618`). Single is 145 and 768 (`MapRenderChunk.cpp:651-654`). The dual branch returns an existing `m_vertexBuf` without resizing (`MapRenderChunk.cpp:610-613`), so a stolen 145-vertex single handed through that branch is the wrong buffer. A dual miss ends on a 290-vertex buffer.
- The victim is absent for the frame. `RenderSinglePass` is the stream that stays: it always builds the debug lines with `BufStream` (`MapRenderChunk.cpp:1013-1024`).

An empty `s_bufList` (pools not built, or `DestroyBufs` has unlinked every `m_bufLink` at `MapRenderChunk.cpp:411`) has no tail to take.

**Pass.** On a steady outdoor camera, terrain `UseStreamingBufs` stays at zero once the pool exists, including when the free lists are empty and the view is larger than the pool. The debug line path still calls `BufStream`.

## All chunks holed

**Hold.** The shared index buffer is bound only for a half whose `m_holes` is 0. Every non-zero mask keeps the indices `CreateIndices` writes today. A chunk with `m_holes == 0xFFFF` never enters that fill.

`CreateIndices` walks the 8×8 cells and skips a cell when its bit is set (`WoW/Source/WorldClient/MapChunk.cpp:302-310`). Each clear cell writes 12 indices, so a clear chunk writes 768 (`MapRenderChunk.cpp:40`, `MapChunk.cpp:318-332`). The mask is 16 bits, one bit per 2×2 cells (`WoW/Source/WorldClient/Map.cpp:286`). One set bit drops four cells, 48 indices, using `holeMaskIndex = (row / 2) * 4 + (col / 2)`.

`m_holes == 0xFFFF` fails the render gate (`WoW/Source/WorldClient/World.cpp:4068-4070`). That chunk does not call `RenderPrep`, so it does not mesh and it does not spawn grass. Grass on a chunk that does render also skips punched cells with the same mask (`MapChunk.cpp:1454-1456`).

A non-zero mask still occupies the full 145-vertex span. After the cell loop, `m_maxIndex` becomes `s_chunkBatch.m_maxIndex + baseVertex` (`MapChunk.cpp:894-900`). `s_chunkBatch.m_maxIndex` is 144 (`MapRenderChunk.cpp:362`). Holes change `m_count`. They do not pack vertices down.

When every rendered chunk has a non-zero mask, the shared buffer has no users. Private index capacity stays the full pool, `768 * count` (`MapRenderChunk.cpp:831`). The hole-free byte drop is zero in that view.

Winding for a clear cell stays `C, TL, BL`, `C, TR, TL`, `C, BR, TR`, `C, BL, BR` (`MapChunk.cpp:318-329`). Occlusion still reads the same mask (`World.cpp:2612`, `MapChunk.cpp:1314`).

**Pass.** A view where every `m_holes` is non-zero draws the punched triangles only. No chunk samples the shared 768-index pattern. A `0xFFFF` chunk contributes no terrain draw and no grass inst.

## Dual-chunk base vertex

**Hold.** The second map chunk's indices use base vertex 145, the same offset `CreateVertices` uses. That base stays 145 when the first half has holes.

`FillLockedBufs` writes chunk 0 at vertex 0 and chunk 1 at vertex 145 (`MapRenderChunk.cpp:164-177`). The index batch starts with `m_maxIndex == 0` (`MapRenderChunk.cpp:187`), so chunk 0's base is 0 (`MapChunk.cpp:883`). Chunk 0 then sets `m_maxIndex` to 144. Chunk 1's base is `144 + 1` (`MapChunk.cpp:883-900`), and its indices are appended at `&indices[m_count]` (`MapRenderChunk.cpp:192-193`).

The first half's hole mask does not move that base. `CreateIndices` still runs for a present `m_mapChunks[0]`, including a `0xFFFF` partner that wrote zero indices, and it still stores 144. The partner is prepped because the surviving half's `RenderPrep` fills both pointers (`MapRenderChunk.cpp:188-193`). `CullChunks` skips `RenderPrep` on the `0xFFFF` chunk itself (`World.cpp:4068`), and the other half still enters.

`AddBatch` always stores the first chunk (`MapRenderChunk.cpp:524-526`). The partner comes from the layer merge (`WoW/Source/WorldClient/MapArea.cpp:132-145`). Orientation `0x2` shifts the second chunk on Y; any other dual flag shifts it on X (`MapRenderChunk.cpp:171-174`). The index base is 145 either way. `flags & 0x3` selects the 290-vertex buffer (`MapRenderChunk.cpp:617-618`).

The shared pattern is the hole-free 768 indices at base 0, and the same pattern with 145 added to each index. A half with a non-zero mask keeps private indices. A mixed pair (one half holed) is one draw (`m_start == 0`, `m_count` is the sum), so the holed half's private indices and the clear half's +145 pattern have to land in that one index range. Adding 145 to a holed first half's private indices is a different mesh: the second half's vertices still start at 145, not at the first half's highest used index.

**Pass.** A dual chunk with a holed first half and a clear second half draws the second half from vertices 145..289. A dual chunk with both halves clear matches the static pattern (768 indices at base 0, then 768 at base 145). Max index of the second half is 289 (`144 + 145`).

## Far clip at the 160 cap

**Hold.** `UpdatePools` keeps radius 160 and slot count 51200. A larger far clip rebuilds that same pool. Chunks past the pool are the terrain miss above, solved by the `s_bufList` tail, with the radius left alone.

```text
radius = 1 - (int)(World::s_farClip * -0.03f)
if (radius > 160) radius = 160
count  = (2 * radius * radius + 1) & ~1
```

(`MapRenderChunk.cpp:821-831`). At radius 160 the product `2 * 160 * 160 + 1` is 51201, and `& ~1` stores 51200. Block count is `count / 2` = 25600 (`MapRenderChunk.cpp:837`). Estimates are `145 * 51200` vertices and `768 * 51200` indices (`MapRenderChunk.cpp:830-831`).

The comparison is `>`, so radius 160 is also the last value that misses the clamp. The uncapped radius becomes 161 at `s_farClip == 5333.33349609375` (`1 - (int)(far * -0.03f)` in float32). Extended far clip is 10000 (`WoW/Source/WorldClient/WorldParam_CVarSlider.h:10`), which would be radius 301 and count 181202 without the clamp. The comment above the clamp is that case (`MapRenderChunk.cpp:823-826`).

`SetFarClip` still marks the pools dirty when the validated value changes (`WoW/Source/WorldClient/World.cpp:1364-1370`), so a move from 6000 to 10000 rebuilds and lands on 51200 again.

The last block is index 25599, `m_vertexCount = 2 * 25599` = 51198 (`MapRenderChunk.cpp:841`). Its dual index `BufCreate` offset is `1536 * 51198` bytes plus a 3072-byte buffer, which ends on the pool size `768 * 51200 * 2`. The vertex side matches: offset `145 * stride * 51198` plus 290 vertices ends on `145 * 51200 * stride`.

**Pass.** At and above the cap, `s_pnEstimateIndex` is `768 * 51200`, `s_chunkBlockArray` has 25600 blocks, and a further far-clip increase does not add a slot. Misses past those slots take the `s_bufList` tail.

## Detail distance above 220

**Hold.** The stored detail distance is 220 when the caller passes anything larger. Fade, the spawn test, and `groundEffectDistValueSqr` all read that stored value.

`SetDetailDoodadDist` assigns 220 when `dist > 220` (`World.cpp:1314-1318`). The comment on the clamp is the far-clip grass rebuild. If the stored value is already 220, the function returns without setting `s_bPoolsDirty` (`World.cpp:1320-1324`). A real change stores the clamped distance, dirties the grass pools, and sets `groundEffectDistValueSqr` to `dist * dist` (48400 at 220).

The CVar callback rejects a value above the slider max before that call (`WoW/Source/WorldClient/WorldParam_CVar.cpp:388-402`). The extended slider max is 220 (`WorldParam_CVarSlider.h:15`), so 221 never reaches `SetDetailDoodadDist` through the CVar. The clamp is what a direct call hits.

Spawn is `detailDoodadDist > m_distToCamera` (`MapChunk.cpp:1611`). At 220, chunks inside 220 yards build grass against the same 512 pairs (`DetailDoodad.cpp:568-569`). That is the empty-pool case. The fade constants are computed from the stored distance:

- Shader: `fade = 1 / (dist * 0.15)`, `m_fade.x = -fade`, `m_fade.y = dist * fade` (`DetailDoodad.cpp:317-321`). At 220 that is `1/33` and `220/33`.
- Fixed function: scale `1 / (dist * 0.15)`, translate Z by `dist * -0.85` (`DetailDoodad.cpp:777-780`). At 220 that is scale `1/33` and Z `-187`.

**Pass.** `SetDetailDoodadDist(221)` leaves `CWorld::detailDoodadDist` at 220. A second call with a larger value does not dirty pools again. Grass past 220 yards is not created, and the 512-pair pool is the same size it is at 70 yards.
