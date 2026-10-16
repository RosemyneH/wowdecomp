# Leak — watch list

Retire paths only. No behavior change in this note. Two promotions leak if they land as sketched: a trivial `realloc` inside `TSFixedArray::ReallocData`, and a silhouette cache in front of `CMapChunk::ClipBufferUpdate`. The other three watch items do not allocate.

## `TSFixedArray::ReallocData`

`ReallocData` always passes `SMEM_FLAG_NOREALLOC` (`Engine/Source/Storm/H/stpl.h:757`, flag at `Engine/Source/Storm/H/storm.h:58`). `SMemReAlloc` then returns null and does not touch the old block (`Engine/Source/Storm/Source/SMem.cpp:85-87`). The caller still owns `olddata`. The failure arm allocates a new block, copy-constructs the kept elements, runs `~T()` on those sources, and `SMemFree`s the old block (`Engine/Source/Storm/H/stpl.h:759-770`). `SMemFree` is `free` (`Engine/Source/Storm/Source/SMem.cpp:57-63`). It does not run destructors. That is why `~T()` sits in front of the free (`Engine/Source/Storm/H/stpl.h:767`).

A trivial-type fast path that calls `realloc` has the same failure contract. `realloc` failure leaves the original pointer allocated. `SMemReAlloc` returns null and does not free it (`Engine/Source/Storm/Source/SMem.cpp:90-97`). The fast path must still `SMemFree` that block once the replacement block holds the elements, which is what `stpl.h:770` does today. Overwriting `m_data` with the null result and returning drops the only pointer.

`ReallocAndClearData` is the pattern to avoid. It assigns `m_data` from `SMemReAlloc` with the flag clear (`Engine/Source/Storm/H/stpl.h:741`). On failure that stores null and leaks the old block, because nothing else holds it.

Order on the failure arm stays: allocate the new block, copy into it, destroy the old elements, then free. Freeing `olddata` before the new block exists, or freeing it again after a successful `realloc` that already consumed the pointer, is the other half of the same bug. A successful `realloc` has already released the old address. `SMemFree` runs only when `realloc` did not take ownership.

Non-trivial `T` does not take the fast path. The type-trait gate has to be trivially destructible, not merely trivially copy-constructible. A trivial copy constructor can still sit on a type whose destructor releases a resource.

Two destructor sites must stay for every non-trivial `T`:

- The tail `[count, m_count)` is destroyed before the block is resized (`Engine/Source/Storm/H/stpl.h:752-754`). `m_count` is still the old count here. `SetCount` writes the new count only after `ReallocData` returns (`Engine/Source/Storm/H/stpl.h:794-800`). A shrink that `realloc`s first, or that lowers `m_count` first, skips those destructors and leaks whatever the dropped elements own. `realloc` will not call them.
- Elements that were copy-constructed into the new block are destroyed on the old block before `SMemFree` (`Engine/Source/Storm/H/stpl.h:765-768`). Skipping `~T()` there leaks the source. The fast path must not replace that pair with `memcpy` plus `free` unless `~T()` is trivial.

Trivial `T` may `realloc` the kept prefix. The tail loop is then a no-op and can stay. The failure arm still frees the old block after the bytes have been copied, and it does not `SMemFree` a pointer `realloc` already consumed.

## Occlusion silhouettes

`ClipBufferUpdate` builds two 9-vertex outlines and writes them straight into the frame buffer (`WoW/Source/WorldClient/MapChunk.cpp:1302-1327`). There is no per-chunk allocation today. `s_clipBuffer[384]` is wiped every scene (`WoW/Source/WorldClient/CWorldSceneRender.cpp:95-98`). `s_clipBufferVerts[9]` is scratch overwritten by the next call (`WoW/Source/WorldClient/CWorldOcclusion.cpp:43`, `WoW/Source/WorldClient/CWorldOcclusion.cpp:336`).

A cache keyed by height and quadrant has to own its copies. `m_height` is not a heap block. It aliases the area file image (`WoW/Source/WorldClient/MapChunk.cpp:209`). `UpdateBufferChunk` reads `heights[indices]` from that image (`WoW/Source/WorldClient/CWorldOcclusion.cpp:331-341`). The file image is `SMemFree`d with the area (`WoW/Source/WorldClient/MapArea.cpp:605-606`, `WoW/Source/WorldClient/MapMem.cpp:12-13`). A cache that holds `m_height`, a `CMapChunk*`, or a pointer into `s_clipBufferVerts` dangles after that free.

Unload always goes through `Purge` then `FreeChunk`:

- One chunk: `CMapArea::PurgeChunk` (`WoW/Source/WorldClient/MapArea.cpp:38-40`).
- Every chunk on the area: `CMapArea::Purge` (`WoW/Source/WorldClient/MapArea.cpp:55-62`).
- Map teardown: `CMap::Unload` calls `PurgeMaps`, which purges each area and then `FreeArea` (`WoW/Source/WorldClient/Map.cpp:5900-5901`, `WoW/Source/WorldClient/Map.cpp:4008-4016`, `WoW/Source/WorldClient/Map.cpp:3099-3102`). Area purge runs first, so the file image dies only after the chunks are already freed.

`CMapChunk::Purge` retires the render chunk, doodads, and liquids (`WoW/Source/WorldClient/MapChunk.cpp:94-115`). It does not know about a silhouette cache. `CMap::FreeChunk` then runs `~CMapChunk` and `ObjectFree` (`WoW/Source/WorldClient/Map.cpp:3115-3118`). The destructor body is empty (`WoW/Source/WorldClient/MapChunk.cpp:1839-1840`), and the chunk address goes back on `chunkHeap`. The next `ObjectAlloc` can reuse it.

Drop every entry for that chunk inside `Purge` or `FreeChunk`, before `ObjectFree`. Both outlines go together: the x quadrant (`base` 0 or 136) and the y quadrant (`base` 0 or 8) from the same `ClipBufferUpdate` (`WoW/Source/WorldClient/MapChunk.cpp:1306-1326`). Leaving one quadrant behind retains the entry for the life of the process.

`UpdateOcclusion` unlinks the chunk from the holed list on every frame (`WoW/Source/WorldClient/World.cpp:2613`, `WoW/Source/WorldClient/World.cpp:2627`). That is not unload. The chunk is still live and `m_height` is still the file image. Clearing the cache there throws away the only reason to cache. The early return when culling is off recycles occluders and skips `ClipBufferUpdate` (`WoW/Source/WorldClient/World.cpp:2593-2603`). Those chunks stay loaded too.

While the chunk stays loaded, a height or quadrant change replaces the one entry. It does not append. Append grows the cache with no matching unload.

## Clear

Portal cover, nameplates, and `Job::ParallelFor` do not allocate on the path the watch list names.

`s_coveredRects` and `s_pendingPortalViews` are function-static. `SetCount(0)` runs destructors and keeps the block (`WoW/Source/WorldClient/MapObj.cpp:1194-1199`, `WoW/Source/WorldClient/MapObj.cpp:1333-1334`, shrink arm at `Engine/Source/Storm/H/stpl.h:1007-1013`). Nested `depth` does not push onto those arrays (`WoW/Source/WorldClient/MapObj.cpp:1238`, `WoW/Source/WorldClient/MapObj.cpp:1289`). A promoted test must free any fresh scratch on every return, including the early return at `WoW/Source/WorldClient/MapObj.cpp:1312`.

`MarkSerialNameplates` only writes `defer` (`WoW/Source/Ui/WorldFrame.cpp:1532-1554`). `s_visibleUnits.clear()` keeps capacity (`WoW/Source/Ui/WorldFrame.cpp:1497`, `WoW/Source/Ui/WorldFrame.cpp:1574`). A model-pointer hash must be cleared with that vector and must not own the `CM2Model` or the unit.

`ParallelFor` waits until workers drop `fn` (`Engine/Source/Base/Job/JobSystem.cpp:132-162`). It does not allocate. Threads are joined in `Shutdown` (`Engine/Source/Base/Job/JobSystem.cpp:111-126`). The wait is what keeps the borrowed `user` pointer alive. Detaching workers to skip the barrier would leave that pointer, and the threads, retired by nothing.
