# Campaign 4 scene — remesh

Outdoor hold for the remesh squad. Same character and the same camera on every before and after sample. The pass is a steady second inside the pools that are already built: grass `FillVertices` and terrain `UseStreamingBufs` stay at zero. A hole-mask spot check on that same frame keeps the hole-free index pattern and the private hole pattern apart.

Campaign budget: `docs/perf-agent-plan.md:128`. Distance cvars stay where they are: `docs/perf-agent-plan.md:177`.

## Hold

**Place.** Open ground on Eastern Kingdoms, Elwynn or Westfall density (`docs/perf-agent-plan.md:32`). Grass is on the ground in front of the camera. The body is outdoors.

**Record once, then freeze.**

| Value | Where |
| --- | --- |
| Body | `World::s_currentWorldPos` (`WoW/Source/WorldClient/World.cpp:1181`) |
| Camera | `CWorldScene::s_activeWorldView` (`WoW/Source/WorldClient/World.cpp:1276`) |
| Look-at | `CWorldScene::camTarget` (`WoW/Source/WorldClient/World.cpp:1282`) |

Write the three vectors down. Later slices load those numbers. The body does not walk and the mouse does not turn. `PrepareAreaOfInterest` rebuilds `chunkRect` from the body position (`WoW/Source/WorldClient/World.cpp:1180`, rect at `1222`). A moving body drops chunks, and the next `AllocBuf` can miss.

**Look.** Eye height, pitched at the horizon. `CullChunks` calls `RenderPrep` for a chunk the frustum keeps (`WoW/Source/WorldClient/World.cpp:4064`) whose hole word is not `0xFFFF` (`4068`). The terrain pool has to cover that set. A camera aimed at the feet takes the whole far-clip disk, and that disk is larger than the slot count below.

**Cvars.** Leave `farclip`, `groundEffectDist`, and `groundEffectDensity` at the values that built the pools. Writing them marks the pools dirty and the next frames refill (`WoW/Source/WorldClient/World.cpp:1305`, `1314`, `1364`). Registered defaults, when nothing has changed them:

| Cvar | Register default | Site |
| --- | --- | --- |
| `farclip` | `350` | `WoW/Source/WorldClient/WorldParam_CVar.cpp:696` |
| `groundEffectDensity` | `16` | `WoW/Source/WorldClient/WorldParam_CVar.cpp:711` |
| `groundEffectDist` | `70.0` | `WoW/Source/WorldClient/WorldParam_CVar.cpp:712` |

`SetDetailDoodadDist` clamps at 220 yards (`WoW/Source/WorldClient/World.cpp:1316`). This scene stays on the live distance. The initial value is 70 (`WoW/Source/WorldClient/World.cpp:136`).

**Flags.**

- `Enable_Chunks` and `Enable_DetailDoodads` are on (`WoW/Source/WorldClient/Types.h:43`, `WoW/Source/WorldClient/Types.h:62`). Terrain draw is gated at `WoW/Source/WorldClient/World.cpp:2664`. Grass is gated at `WoW/Source/WorldClient/MapChunk.cpp:1611`.
- `Enable_ShowNormals` is off (`WoW/Source/WorldClient/Types.h:72`). That bit is the line path `RenderSinglePass` (`WoW/Source/WorldClient/World.cpp:3719`, `WoW/Source/WorldClient/MapRenderChunk.cpp:1013`). It is not the `UseStreamingBufs` counter, and it stays out of this frame.
- `CWorld::detailDoodadTest` stays 0 (`WoW/Source/WorldClient/MapChunk.cpp:1373`). The test return skips placement, and the grass counter is then zero because there is no grass.

## Pools the hold has to fit

**Terrain.** `CMapRenderChunk::UpdatePools` (`WoW/Source/WorldClient/MapRenderChunk.cpp:805`):

```
radius = 1 - (int)(World::s_farClip * -0.03)
if (radius > 160) radius = 160
slots  = (2 * radius * radius + 1) & ~1
```

(`WoW/Source/WorldClient/MapRenderChunk.cpp:821`). At the registered `350`, `350 * -0.03` is `-10.5`, the cast truncates toward zero to `-10`, radius is `11`, and slots are `(2 * 121 + 1) & ~1` = **242**. The cap at 160 is the same function (`825`). Each slot is 145 vertices and 768 indices (`830`).

A single chunk takes one slot: 145 vertices (`WoW/Source/WorldClient/MapRenderChunk.cpp:653`) and 768 indices (`654`). A dual chunk (`m_unkFlags & 0x3`) takes the whole block: 290 vertices (`617`) and 1536 indices (`618`). The dual branch starts at `597`. Count a dual as two slots. Vertical pairs use flag `1`, horizontal pairs flag `2` (`WoW/Source/WorldClient/MapArea.cpp:122`).

`AllocBuf` returns null when the matching free list is empty (`WoW/Source/WorldClient/MapRenderChunk.cpp:600`, `635`). `RenderSetup` then calls `UseStreamingBufs` (`930`). `RenderPrep` links `s_bufList` to head only after a buffer exists (`992`). On this hold the free list still has a slot for every visible chunk, so the steady frames do not stream and do not need a later LRU steal.

**Grass.** `CDetailDoodad::UpdatePools` builds **512** vertex/index pairs (`WoW/Source/WorldClient/DetailDoodad.cpp:568`). The fill queue stops at the same 512 (`606`, `681`). A chunk builds grass while `detailDoodadDist > m_distToCamera` (`WoW/Source/WorldClient/MapChunk.cpp:1611`). That distance is the camera-plane distance (`WoW/Source/WorldClient/MapChunk.cpp:465`, stored at `582`). An inst has at most four batches (`WoW/Source/WorldClient/DetailDoodad.cpp:835`). The 512-slot pool is the one that covers this radius (`DetailDoodad.cpp:568`).

A short pool leaves both pointers null (`WoW/Source/WorldClient/DetailDoodad.cpp:129`). `Draw` then returns before `FillVertices` (`888`). That return is a miss. The fill counter stays zero because the batch was skipped.

## Window

Freeze body and camera. Wait through one second so each visible chunk has `m_chunkBuf` and each live grass buffer has `m_hasData`.

A chunk that draws every frame keeps its buffer. `RenderSetup` zeros `m_lastUpdateTime` (`WoW/Source/WorldClient/MapRenderChunk.cpp:912`). `ProcessRenderChunkUpdateList` calls `FreeBuf` only after two seconds unseen (`WoW/Source/WorldClient/Map.cpp:1933`).

A grass inst that draws every frame keeps its buffers. `RenderDetailDoodads` zeros `m_age` (`WoW/Source/WorldClient/World.cpp:3586`). `ProcessDetailDoodadUpdateList` calls `FreeBuffers` only after `m_age` passes 10 (`WoW/Source/WorldClient/Map.cpp:1910`).

The sample is every later one-second log line at the recorded position and facing. The warmup second may be non-zero. It is not the pass.

## Pass

On each sample line, both counters stay at **zero**.

| Counter | A zero means | Call sites |
| --- | --- | --- |
| `CDetailDoodadInstBatch::FillVertices` | No grass batch rebuilt its vertices | Defined at `WoW/Source/WorldClient/DetailDoodad.cpp:340`. Called from `FillMappedRange` (`617`) and `FillVerticesOnMain` (`632`). |
| `CMapRenderChunk::UseStreamingBufs` | No terrain chunk locked the stream ring | Defined at `WoW/Source/WorldClient/MapRenderChunk.cpp:889`. Called from `RenderSetup` (`934`). |

`FillVertices` runs when `m_hasData` is clear (`WoW/Source/WorldClient/DetailDoodad.cpp:673`, and `Draw` at `892`). Taking a pool buffer clears that flag (`124`). A steady hit leaves it set, so both grass call sites stay idle.

`UseStreamingBufs` runs when `m_chunkBuf` is null (`WoW/Source/WorldClient/MapRenderChunk.cpp:930`). A steady hit takes the vertex and index pointers at `931` and does not enter `934`.

The line fails the scene when either counter is non-zero, or when a textured grass batch returns at `WoW/Source/WorldClient/DetailDoodad.cpp:888` because its buffers are null. The camera is outside the current pool. Yaw toward open ground at eye level on the same spot. Leave `farclip`, `groundEffectDist`, and `groundEffectDensity` alone.

## Hole-mask spot check

Same frozen frame. Two chunks that `CullChunks` prepped. A word of `0xFFFF` never reaches `RenderPrep` (`WoW/Source/WorldClient/World.cpp:4068`). `m_holes` is the `uint16_t` at `WoW/Source/WorldClient/MapChunk.h:178`.

**Hole-free.** `m_holes == 0`. `CreateIndices` writes 8×8 quads, 12 indices each, **768** `uint16`s (`WoW/Source/WorldClient/MapChunk.cpp:306`). The first quad, base 0, is:

```
9, 0, 17,  9, 1, 0,  9, 18, 1,  9, 17, 18
```

That is center, top-left, bottom-left, then the other three triangles (`318`). The highest index is 144, which is the 145-vertex grid.

**Dual, both halves hole-free.** The second map chunk starts at base `m_maxIndex + 1` (`WoW/Source/WorldClient/MapChunk.cpp:883`), which is **145**, and its indices start at offset 768 (`WoW/Source/WorldClient/MapRenderChunk.cpp:188`). Flag `2` offsets the second half in Y; otherwise the offset is in X (`171`). The shared hole-free buffer is this 768-index pattern, with the dual half at +145. The index-pool byte drop is this share, measured on this camera (`WoW/Source/WorldClient/MapRenderChunk.cpp:831`).

**Partial mask.** `m_holes` is neither 0 nor `0xFFFF`. Bit `i` is `CMap::s_holeMask[i]`, which is `1 << i` (`WoW/Source/WorldClient/Map.cpp:286`). It covers the 2×2 quads whose `(row / 2) * 4 + (col / 2) == i` (`WoW/Source/WorldClient/MapChunk.cpp:308`). Each set bit drops 4×12 = **48** indices. The count for that map chunk is `768 - 48 * popcount(m_holes)`. Those cells are also absent from the grass inst (`1454`). This chunk keeps a private index buffer. It does not match the hole-free pattern.

Record, for both chunks, `m_cOffset`, the raw `m_holes` word, the index count, and the second-half base when the render chunk is dual. The grass seed is `m_cOffset.x | (m_cOffset.y << 16)` (`WoW/Source/WorldClient/MapChunk.cpp:1366`). The spot check reads that seed. It does not reseed. The same two chunks are the before and the after.
