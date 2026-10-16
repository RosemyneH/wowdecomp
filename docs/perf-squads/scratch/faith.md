# Faith — Campaign 5 cull and visibility scratch

Reviewed the live visibility flush and exterior doodad cull against the serial bodies they replaced (`AddMapChunk`, `CullAndBucketChunk`, `CullDoodadsExterior`).

**Verdict: PASS** on the live scratch. Parallel results, bucket assignment, frustum and occlusion thresholds, and the unloaded-doodad barrier match.

**FAIL** swap-with-last on `s_visQueue`. Do not land it. A generation stamp that leaves order and `back()` alone passes. Swap-with-last on `s_ownedFills` passes the visible set.

## Parallel visibility results — PASS

`RunChunkVisibility` writes only `jobs[i]` and that job’s liquid slice (`WoW/Source/WorldClient/MapChunk.cpp:455-504`). Liquid ranges are assigned on the calling thread before the job (`MapChunk.cpp:549-564`) and do not overlap. `CFrustum::Cull` only reads planes (`Engine/Source/Tempest/CFrustum.cpp:15-29`). `CChunkLiquid::GetAaBox` only reads the owner box and `m_height` (`WoW/Source/WorldClient/MapChunkLiquid.cpp:60-63`).

`Job::ParallelFor` returns after every range and every worker has dropped the job (`Engine/Source/Base/Job/JobSystem.cpp:156-162`). `m_distToCamera`, `LinkChunkBucket`, and `LinkLiquidBucket` run after that join, in request order (`MapChunk.cpp:578-592`). Which worker took the range does not change the stored result.

`CullDoodadRange` writes only `culls[i].inFrustum` and `culls[i].occluded` (`World.cpp:3910-3918`). Under 32 doodads it calls that same function on the caller (`World.cpp:3967-3968`). `AddDoodadDefModelToModelScene`, `m_culled`, and `SetAnimating` run after the join, in vector order (`World.cpp:3971-3990`).

The snapshot is taken at the first enqueue of the flush (`MapChunk.cpp:416-418`), which is `CMapArea::Update` (`MapArea.cpp:259-260`), after `CWorldScene::Update` has published the frustum and `farCornerIndex` (`World.cpp:2892-2902`). The original tests ran at that same call. `FlushQueuedVisibility` consumes the copy (`MapChunk.cpp:521`, `:567-573`). `CMapRenderChunk::UpdatePools` flushes before `CullSortTable` (`MapRenderChunk.cpp:806`, `CWorldSceneRender.cpp:92`, `World.cpp:4210`), so `m_distToCamera` is stored before `GetFadeLevelForDistance` (`World.cpp:4065`).

## Sort-bucket linking — PASS

Chunk buckets are `AddMapChunkToRenderList` (`World.cpp:2296-2302`), copied at `MapChunk.cpp:427-433`. Liquid buckets are `AddToDistanceBucket` (`World.cpp:2307-2320`), copied at `MapChunk.cpp:436-449`.

| Rule | Chunk list | Liquid list |
| --- | --- | --- |
| Plane | `camPlaneXY` dot far-corner position | same |
| Scale | `0.029999999f` (`MapChunk.cpp:431`) | `0.03f` (`MapChunk.cpp:441`) |
| `distance <= 0` | bucket 0 | bucket 0 |
| `fint_(scaled - 0.5f) >= 64` | not linked | not linked |
| Otherwise | `m_mapChunkList.LinkToTail` | `m_liquidList.LinkToTail` |

`0.03f` and `0.029999999f` are the same `float`. The corner is `cornerVertexIndex[farCornerIndex]` plus `m_topLeftCoords`, with Z from `m_height` for chunks and clamped to `[m_height.x, m_height.y]` for liquids (`MapChunk.cpp:467-470`, `:498-502`). That clamp is the old liquid corner (`(camZ <= top ? camZ : top) < bottom ? bottom : (camZ > top ? top : camZ)`).

A chunk the frustum rejects is not linked and does not receive `m_distToCamera` (`MapChunk.cpp:458-472`, `:581-583`). Liquids are tested even when the chunk is rejected, which is the old split between `AddMapChunk` and `CullAndBucketChunk`. `LinkToTail` unlinks first (`Engine/Source/Storm/H/stpl.h:482-484`), so a second request for the same chunk moves it inside the bucket it already computed. The live queue is filled in area-walk order (`MapArea.cpp:243-260`) and linked in that order.

## Frustum and occlusion thresholds — PASS

Box test: a plane closer than `-0.019444443f` returns `0`, otherwise `3` (`CFrustum.cpp:24-29`). The old `FrustumCull` is `Cull == 0` (`World.cpp:1848-1849`). The flush keeps a chunk when `Cull != 0` (`MapChunk.cpp:460`) and drops a liquid when `Cull == 0` (`MapChunk.cpp:481`, `:494`). Inverted liquid bounds (`t.x < b.x`) still drop every liquid in the chunk (`MapChunk.cpp:481-488`).

Sphere test: outside when the center is farther than `-radius` past a plane (`CFrustum.cpp:68-76`, `World.cpp:2047-2052`). No box epsilon. `CullDoodadRange` uses that call (`World.cpp:3912`).

Occlusion: `QueryBuffer` returns `0` when culling is off, the radius is tiny, `camVec.z` is outside `±0.89999998f`, `view.z < 50`, or any covered column is above the stored horizon. It returns `2` when the span is covered (`CWorldOcclusion.cpp:486-524`). Exterior doodads still treat `>= 2` as occluded (`World.cpp:3916-3917`). The animate fallback is still `m_staticFlags & 0x400` or distance squared `< 100` (`World.cpp:3986`).

Chunk occlusion is not in the flush. `CullChunks` still requires `Cull(m_bbox)`, `!QueryVolumes(&m_sphere)`, and `!QueryBuffer(&m_bbox, 0)`, then the same pair on `m_bbox2` (`World.cpp:4064-4067`).

## Unloaded doodad barrier — PASS

The barrier stays on the calling thread, before a doodad is pushed, and before `ParallelFor` (`World.cpp:3938-3943`, then `:3965`).

```3938:3943:WoW/Source/WorldClient/World.cpp
        if (!doodadDef->m_model || !(doodadDef->m_flags & MAPOBJ_FLAG_PREPARED)) {
            CAaBox box;
            CAaBox bounds = doodadDef->m_model->m_shared->m_boundingBox;
            CWorldMath::TransformAABox(doodadDef->m_mat, bounds, box);
            CWorldScene::s_barrier.AddBarrier(box, 10.0f);
            continue;
```

That is the old `else` of `m_model && PREPARED`: same box, same `10.0f` radius. A fade below the chunk fade level still skips the doodad, and `CMap::header.m_flags & 0x8` still stops the walk (`World.cpp:3930-3936`). Doodads already queued are tested after the stop; doodads not yet visited are not. A matching `m_cullPassStamp` still skips the doodad with no second barrier and no second scene add (`World.cpp:3946-3947`).

## Swap-erase — FAIL on the visibility queue

Live cancel is a stable erase. It removes every request whose chunk is being purged and leaves the rest in order (`MapChunk.cpp:390-397`). `Purge` also unlinks `m_sortListLink` (`MapChunk.cpp:95`, `:117`). The survivors are the same set the flush would have linked. **PASS** for who is visible.

Swap-with-last on `s_visQueue` does not pass.

`CullAndBucketChunk` stamps liquids only when `s_visQueue.back().chunk == this` (`MapChunk.cpp:1123-1124`). Otherwise it pushes a second request with `linkChunk == 0` (`MapChunk.cpp:1128`). Those two calls are adjacent (`MapArea.cpp:259-260`), so a purge of some other chunk does not land inside that window today. A swap that runs while the tail is still `wantLiquids == 0` moves that tail, misses the stamp, and splits one visit into two records. The flush still links the chunk from the first and the liquids from the second, so the set matches. The `LinkToTail` sequence does not. `CullChunks` and `CullLiquid` walk that sequence (`World.cpp:4058`, `:2568`).

A loop that always increments after the swap is worse. The tail can be another request for the same purged chunk. The swap deposits it in the hole, the index moves past it, and the flush links that chunk again after `Purge` unlinked it (`MapChunk.cpp:117`, then `MapChunk.cpp:583`). That chunk is visible. It must not be.

`s_ownedFills` is a different vector. `QueueOwnedFill` refuses a second pointer (`MapRenderChunk.cpp:236-239`). `FlushOwnedFills` consumes the set (`MapRenderChunk.cpp:250`). Overlap is any locked range that intersects any earlier lock (`MapRenderChunk.cpp:293-306`); pair order does not change that boolean. A swap that removes only the cancelled render chunk leaves the same fills. **PASS** for who is visible. It still must not skip a swapped-in match.

A generation stamp on `VisRequest` passes when the flush drops stamped requests before `LinkChunkBucket` (`MapChunk.cpp:583`) and `LinkLiquidBucket` (`MapChunk.cpp:590`), leaves the relative order of the rest, and does not change `back()`. `s_visSnap.valid` stays tied to zero live requests (`MapChunk.cpp:399-401`, `:522`). Do not reuse `m_cullPassStamp` (`World.cpp:3946`). That stamp is why a doodad already handled this pass is skipped.
