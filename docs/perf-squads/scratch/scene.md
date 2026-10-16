# Campaign 5 scene — outdoor fly-around

Outdoor flight at Elwynn / Westfall density. The pass is the malloc budget and an O(1) cancel. The picture below is what that flight still shows.

## Flight

Same character position and camera as the Campaign 0 outdoor run, then one repeated path across open ground. Terrain, doodads, and grass stay in view. The path crosses chunk boundaries, so chunks are purged and render chunks are destroyed.

Stay outside. The frame under test is the branch where `s_viewerMapObjDef` is null and `CullSortTable` runs on the full rectangle (`WoW/Source/WorldClient/CWorldSceneRender.cpp:138-148`). Doodads, culling, water, and detail doodads stay enabled (`Enable_Doodads`, `Enable_Culling`, `Enable_Water`, `Enable_DetailDoodads` in `WoW/Source/WorldClient/Types.h:42`, `:47`, `:66`, `:62`).

`CMapRenderChunk::UpdatePools` flushes queued visibility before that cull (`WoW/Source/WorldClient/CWorldSceneRender.cpp:92`, `WoW/Source/WorldClient/MapRenderChunk.cpp:806`).

## Pass

After an arena has taken its high-water allocation, the rest of this flight keeps `malloc` at zero there. A later chunk past that high-water mark is the one spike for that arena. Every cancel on the flight is O(1).

| Check | Where the bytes move today |
| --- | --- |
| Visibility flush | `jobs(requests.size())` and `liquids(liquidTotal)` allocate on every non-empty flush (`WoW/Source/WorldClient/MapChunk.cpp:524`, `:549`). The swap at `:519-520` returns `s_visQueue` with empty capacity, so the next `push_back` allocates (`:424`). |
| Doodad flush | `s_doodadCulls.clear()` then `push_back` for every exterior chunk (`WoW/Source/WorldClient/World.cpp:3925`, `:3957`). |
| Cancel | `DropQueuedVisibility` erases in the middle of `s_visQueue` (`WoW/Source/WorldClient/MapChunk.cpp:390-396`) from `CMapChunk::Purge` (`:94-95`). `CancelOwnedFill` erases in the middle of `s_ownedFills` (`WoW/Source/WorldClient/MapRenderChunk.cpp:206-212`) from `~CMapRenderChunk` (`:785-786`). |

An empty visibility queue returns before those vectors are built (`WoW/Source/WorldClient/MapChunk.cpp:515-516`).

## On screen

### One snapshot, then the same chunks and the same water

The first request in a queue captures one frustum, camera plane, far-corner index, and camera Z (`WoW/Source/WorldClient/MapChunk.cpp:404-424`). Every job in that flush uses it. Workers write per-chunk results. Sort lists are linked after `ParallelFor` returns (`:577-593`).

A chunk request (`linkChunk`) stays when `frustum.Cull(m_bbox)` is non-zero (`:460`). Its camera distance is the plane distance of the nearest AABB corner (`:462-465`), stored on `m_distToCamera` (`:582`). The point that selects the sort bucket is the far corner (`:467-470`, `:583`).

`LinkChunkBucket` (`:427-434`):

- Distance `<= 0` links bucket 0.
- Otherwise the bucket is `fint(distance * 0.029999999f - 0.5)`.
- A bucket `>= 64` stays unlinked.
- Every other chunk is `LinkToTail` on `m_mapChunkList`.

`LinkLiquidBucket` (`:436-449`):

- Distance `<= 0` links bucket 0.
- Otherwise the bucket is `fint(distance * 0.03f - 0.5)`.
- A bucket `>= 64` is dropped.
- A liquid with `visible == 1` (`:503`) is `LinkToTail` on `m_liquidList` (`:589-590`).

`CullSortTable` walks buckets 0 through 63 (`WoW/Source/WorldClient/World.cpp:4217-4224`), so a nearer band is drawn before a farther one. `CullLiquid` draws that bucket’s liquids in link order (`:2568-2582`).

A purged chunk leaves the queue (`WoW/Source/WorldClient/MapChunk.cpp:94-95`, `:390-396`). After the flush it is absent from both lists. Every chunk that stayed queued is still tested, and it still lands in the same bucket. A destroyed render chunk is the one `CancelOwnedFill` removes (`WoW/Source/WorldClient/MapRenderChunk.cpp:785-786`); every render chunk still queued is still filled.

### Ground on the tight box, doodads on the wide box

`CullChunks` (`WoW/Source/WorldClient/World.cpp:4064-4070`) only sees chunks the visibility flush linked.

- Doodads run when `frustum.Cull(m_bbox)` passes, `QueryVolumes` on the chunk sphere is clear, and `QueryBuffer(m_bbox, 0)` is clear.
- Terrain runs when that passed and `frustum.Cull(m_bbox2)` passes and `QueryBuffer(m_bbox2, 0)` is clear, and `m_holes != 0xFFFF`.

At the edge of the view, doodads remain on a chunk whose tighter box dropped the ground. A chunk whose hole mask is `0xFFFF` contributes no terrain. A partial hole still draws.

### Same doodads

`CullDoodadsExterior` (`WoW/Source/WorldClient/World.cpp:3924`) is the flush that refills `s_doodadCulls` (`:3905`). Sphere tests write `inFrustum` and `occluded`. Model flags, the model-scene submit, and animation run after the parallel join (`:3964-3990`).

Fade uses the chunk distance from the visibility job (`:4065`) and `GetFadeLevelForDistance` (`WoW/Source/WorldClient/WorldView.cpp:15-38`). The squared bands are `900`, `10000`, `40000`, and `562500` (`WorldView.cpp:11`): 30, 100, 200, and 750 yards. A doodad whose `m_fadeLevel` is below that band is skipped (`World.cpp:3930`). On a map whose header has `0x8` set, that skip ends the list (`:3931-3932`).

A doodad already stamped with `s_cullPassStamp` is skipped (`:3946-3948`). The outdoor frame increments that stamp once (`WoW/Source/WorldClient/CWorldSceneRender.cpp:139`), so a doodad shared by two chunks is submitted once.

`FrustumCull` reports the sphere outside when the frustum cull returns 0 (`WoW/Source/WorldClient/World.cpp:2047-2052`, `:3912-3913`). Inside the frustum, `QueryBuffer(center, radius, 0x10) >= 2` marks the doodad occluded (`:3916-3917`). That query returns 2 when the sphere sits behind the silhouette. It returns 0 when culling is off, the radius is ~0, camera pitch is steeper than `0.9`, view-space Z is under 50, or any covered column is in front of the buffer (`WoW/Source/WorldClient/CWorldOcclusion.cpp:485-524`).

Drawn doodads are in the frustum and not occluded (`WoW/Source/WorldClient/World.cpp:3978-3980`). `m_culled` is cleared for every in-frustum doodad, including occluded ones (`:3974-3976`). Doodads that were not submitted still animate when `m_staticFlags & 0x400`, or when squared distance to the camera is under `100` (10 yards) (`:3984-3990`).

### Same barrier around doodads that are not loaded

A doodad with no model, or without `MAPOBJ_FLAG_PREPARED` (`WoW/Source/WorldClient/Types.h:89`), is not submitted. Its bounds go to `s_barrier.AddBarrier` with radius `10.0f` (`WoW/Source/WorldClient/World.cpp:3938-3943`). The barrier keeps that box when the viewer is within 10 yards of the box grown by `0.05`, and it builds the five faces from a floor one yard below (`WoW/Source/WorldClient/Barrier.cpp:195-244`). With barrier mode on, those contacts still render the sparkle models (`Barrier.cpp:306-336`). A prepared model stays on the draw path above.
