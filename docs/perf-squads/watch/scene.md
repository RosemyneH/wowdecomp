# Watch scene — three items against the six campaigns

The four captures stay on one character position and one camera per scene. This note is how a later log, on those poses, shows whether outdoor grow traffic, many small `ParallelFor` waits, and occlusion updates while turning belong above campaigns 1–6. This pass changes no behavior and adds no counter.

## Poses

| Scene | Holds |
| --- | --- |
| Outdoor | Elwynn / Westfall density: terrain, doodads, grass. Campaigns 2–6 read this pose. |
| City | Many skinned and rigid M2s. Campaign 1’s camera. |
| Interior | Portal-heavy Ironforge. These three are not decided here. |
| Crowd | Nameplates. A different watch row. |

Campaign 0 already logs zone times, alloc counts, stream bytes, `vkAllocateMemory` count, uniform uploads, descriptor binds, terrain batch size, and draw count. These three have no field of their own. Rank a second after the scene is resident. The load second is the spike.

## The six costs on that same second

An item is above the six when its own cost, on the pose below, is larger than every row here.

| Campaign | Cost on the shared second | Site |
| --- | --- | --- |
| 1 | Rigid section vertex and index rewrite, and `DestroyBuffers` when the instance cap steps up | `CM2SceneRender.cpp:644`, `CM2SceneRender.cpp:667`, `M2Shared.cpp:779` |
| 2 | Uniform uploads and descriptor binds | `CGxDeviceVulkan.cpp:1377`, `CGxDeviceVulkan.cpp:1442` |
| 3 | One `TerrainCapture` per chunk, then flush scratch | `MapRenderChunk.cpp:2556`, `MapRenderChunk.cpp:2572` |
| 4 | Terrain stream refill and grass `FillVertices` | `MapRenderChunk.cpp:934`, `DetailDoodad.cpp:340` |
| 5 | `jobs`, `liquids`, and `s_doodadCulls` allocations | `MapChunk.cpp:524`, `MapChunk.cpp:549`, `World.cpp:3957` |
| 6 | `vkAllocateMemory` while crossing the outdoor scene | `VkContext.cpp:1003` |

## Outdoor grow traffic

`TSFixedArray::ReallocData` (`Engine/Source/Storm/H/stpl.h:749`) always passes `SMEM_FLAG_NOREALLOC` (`stpl.h:757`). A live pointer makes `SMemReAlloc` return null (`Engine/Source/Storm/Source/SMem.cpp:85`), and the caller allocates, copy-constructs, and frees (`stpl.h:760`). A null pointer takes `SMemAlloc` and copies nothing (`SMem.cpp:79`). `TSGrowableArray::SetCount` reaches that copy only when the new count passes `m_alloc` (`stpl.h:993`). `Clear` drops the pointer and the capacity (`stpl.h:722`).

**Pose.** Outdoor campaign camera, frozen, for two seconds after the zone is resident. Then the fly campaign 6 already uses, as its own pair of seconds.

**How to tell.**

- Two frozen seconds with the same alloc count have no grow in progress. The allocs that remain are the per-frame vectors in the campaign 3 and campaign 5 rows.
- Copy-grow bytes (live old pointer, count above `m_alloc`) belong above the six when those bytes, on one of those seconds, beat every row in the table.
- A fly that lifts alloc counts for a tile and then sits back on the frozen floor is the spike. That spike stays on the watch list.
- `CChunkLiquid::Batch` clears and reallocates 32 slots (`WoW/Source/WorldClient/MapChunkLiquid.cpp:426`) only while `RenderPrep` has no instance (`MapChunkLiquid.cpp:570`). That is the first water second.
- `s_chunkBlockArray.SetCount` runs when terrain pools are dirty (`MapRenderChunk.cpp:808`, `MapRenderChunk.cpp:837`). That is the campaign 4 pool build.
- `DestroyBuffers` (`M2Shared.cpp:779`) is campaign 1’s instance step. `vkAllocateMemory` (`VkContext.cpp:1003`) is campaign 6. Both can move on the fly. Take them out before the remainder is grow.

## Many small ParallelFor waits

`ParallelFor` publishes, then waits until every worker has checked in (`Engine/Source/Base/Job/JobSystem.cpp:158`). The split is `min(count, workerCount + 1)` (`JobSystem.cpp:138`). Workers with no range still increment `finished`. A small count pays the full barrier.

Two sites do this many times a frame, each with a small count:

| Call | Gate | Repeats |
| --- | --- | --- |
| `FillDoodadSkin` | `instances >= 8` (`CM2SceneRender.cpp:794`). Under that, the batch stays on the caller (`CM2SceneRender.cpp:797`). | Once per doodad batch that missed `DrawRigidDoodads` (`CM2SceneRender.cpp:717`). |
| `CullDoodadRange` | `s_doodadCulls.size() >= 32` (`World.cpp:3965`). Under that, the chunk stays serial (`World.cpp:3968`). | Once per visible exterior chunk (`World.cpp:4065`). |

One call a frame is a different cost, on the same barrier: chunk visibility (`MapChunk.cpp:578`), terrain fills (`MapRenderChunk.cpp:333`), grass fills (`DetailDoodad.cpp:732`), model animate (`Engine/Source/Model2/M2Scene.cpp:1728`). Nameplates (`WoW/Source/Ui/WorldFrame.cpp:1590`) are the crowd watch row. Interior doodads stay on `CullDoodadsInterior` (`World.cpp:3996`).

**Pose.** The frozen outdoor camera and the frozen city camera, the same pair campaigns 1 and 5 use.

**How to tell.**

- The sample is the wait at `JobSystem.cpp:158`. Time inside `FillDoodadSkin` (`CM2SceneRender.cpp:598`) or `CullDoodadRange` (`World.cpp:3907`) stays with that work.
- Skin waits sit inside campaign 1 while the rewrite is still in the zone. On these same two cameras, after rigid batches move matrix bytes only and a skinned section skins once, the wait belongs above the six when it is still larger than every row in the table. A Campaign 0 M2 zone that still includes the rewrite is campaign 1.
- Chunk-cull waits are that same barrier, and they are not the mesh rewrite. On the frozen outdoor second, many calls sitting on the 32-gate belong above the six when the summed waits beat the table. The `s_doodadCulls` vector (`World.cpp:3925`) is campaign 5’s malloc. That malloc can already be flat while the waits remain.

## Occlusion updates while turning the camera

`CullSortTable` calls `UpdateOcclusion` once per sort bucket (`World.cpp:4224`). With culling enabled (`World.cpp:2593`), a hole-free chunk updates inside the near/far band (`World.cpp:2615`), and a holed chunk updates on every visit (`World.cpp:2628`). Outdoors that band runs from `s_occlusionNearDepth = -10000` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:146`) through `s_occlusionFarDepth = farClip - 33.333332` (`CWorldSceneRender.cpp:85`).

`ClipBufferUpdate` (`WoW/Source/WorldClient/MapChunk.cpp:1302`) builds two 9-index outlines. The row starts at 136 when the eye is past `camTarget` on x (`MapChunk.cpp:1306`). The column starts at 8 when the eye is past `camTarget` on y (`MapChunk.cpp:1318`). Those are the same comparisons as `nearCornerIndex` (`World.cpp:2907`). The indices walk the 145-point grid (`MapChunk.cpp:64`, `MapChunk.cpp:256`). `UpdateBufferChunk` projects each outline through `s_clipBufferMatrix` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:331`, `CWorldOcclusion.cpp:342`). That matrix is the look-at of the eye (`World.cpp:2918`). `|camVec.z| > 0.9` returns before the project (`CWorldOcclusion.cpp:332`). Heights come from `m_height` (`MapChunk.cpp:1314`). A turn leaves them alone. WMO occluders write two points (`CWorldOcclusion.cpp:606`) and are a different update.

**Pose.** One frozen second on the outdoor campaign camera, horizon pitch so `|camVec.z| <= 0.9`. Then one yaw from that camera with the character position held. Walking and flying stay out of this pair.

**How to tell.**

- The sample is time in `CMapChunk::ClipBufferUpdate`.
- On the yaw second, alloc counts, stream bytes, `vkAllocateMemory`, and terrain batch size that match the frozen second leave campaigns 3, 4, 5, and 6 where they were. `UseStreamingBufs` runs when `m_chunkBuf` is null (`MapRenderChunk.cpp:930`). A yaw leaves that pointer in place.
- Uniform uploads, descriptor binds, and draw count can move: the view packed at `CGxDeviceVulkan.cpp:1359` changed, and the visible set changed. Those fields are campaigns 1 and 2. The yaw belongs to occlusion when `ClipBufferUpdate` alone beats the table, including those two fields as measured on the yaw second.
- A frozen second whose `ClipBufferUpdate` time already beats the table is already above the six. The yaw is the second that moves the quadrant and the clip matrix: height and quadrant, the cache the watch row names. A yaw that adds nothing past the frozen second leaves the item on the watch list.

## This pass

No cache, no gate change, no new log field. The poses above are the comparison. A squad starts when a log on these poses puts the item above the six.
