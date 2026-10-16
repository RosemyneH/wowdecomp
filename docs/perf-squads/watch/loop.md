# Watch squad — Loop

**DEFERRED**

No frame log ranks a watch item above campaigns 1–6. `TSFixedArray::ReallocData`, `Job::ParallelFor`, and `CMapChunk::ClipBufferUpdate` do not call `OsGetAsyncTimeClocks`, and nothing in the tree dumps their grows, waits, or silhouette time. The squad stays closed until a one-second Campaign 0 line shows one of those three above the six campaign samples on that same line.

Campaigns 1–6 are the bar: M2 mesh traffic, per-draw uniform pack, terrain multipass, terrain and grass remesh, cull scratch, Vulkan buffer lifetime.

Each item reopens on one measurement. Clocks compare with campaign zone times. The grow count compares with campaign alloc counts.

## Portal cover test

`CMapObj::RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1165`) walks the group's portals, then at depth 0 tests each pending view against the covered rects (`MapObj.cpp:1316-1326`). The inner loop stops on the first overlap (`MapObj.cpp:1323-1325`).

That scan runs only when `depth == 0` and `s_pendingPortalViews` is non-empty (`MapObj.cpp:1312-1314`). Pending views are pushed on the exterior root pass (`MapObj.cpp:1284-1290`). A portal that faces away on that same pass appends one covered rect, `(0,0)-(1,1)` (`MapObj.cpp:1238-1244`). Both arrays reset at the start of that root pass (`MapObj.cpp:1197-1199`) and after the scan (`MapObj.cpp:1333-1334`).

**Reopen:** one-second sum of `OsGetAsyncTimeClocks` spent in `MapObj.cpp:1316-1326` on the interior scene, greater than every campaign 1–6 zone on that line.

## Nameplates

`MarkSerialNameplates` (`WoW/Source/Ui/WorldFrame.cpp:1532`) flags units whose model stays on this thread (`WorldFrame.cpp:1535-1539`), then compares each remaining unit's `m_worldModel` with every later unit (`WorldFrame.cpp:1541-1553`). `PostAnimateVisibleUnits` calls it once after the visible-unit enum (`WorldFrame.cpp:1574-1576`).

**Reopen:** one-second sum of `OsGetAsyncTimeClocks` spent in `WorldFrame.cpp:1541-1553` on the crowd scene, greater than every campaign 1–6 zone on that line.

## TSFixedArray grows

`TSFixedArray::ReallocData` always passes `SMEM_FLAG_NOREALLOC` (`Engine/Source/Storm/H/stpl.h:757`; the flag is `Engine/Source/Storm/H/storm.h:58`). When the old block is live, `SMemReAlloc` returns null (`Engine/Source/Storm/Source/SMem.cpp:85-86`) and `ReallocData` allocates a new block, copy-constructs, and frees the old one (`stpl.h:759-770`). The copy loop is the `olddata` branch (`stpl.h:762-768`). A first allocation comes back from `SMemAlloc` (`SMem.cpp:79-80`) with `m_data` already set, so it stays out of that branch.

`TSGrowableArray` calls `ReallocData` when the new count exceeds `m_alloc` (`stpl.h:829`, `stpl.h:857`, `stpl.h:907`, `stpl.h:932`, `stpl.h:953`, `stpl.h:973`, `stpl.h:993`). A smaller `SetCount` destructs elements and leaves `m_alloc` in place (`stpl.h:1007-1013`). `ReallocAndClearData` passes flags `0` (`stpl.h:741`) and is a separate path.

**Reopen:** one-second count of `ReallocData` calls that enter `stpl.h:762`, greater than every campaign 1–6 alloc count on that line.

## ParallelFor waits

`Job::ParallelFor` (`Engine/Source/Base/Job/JobSystem.cpp:132`) publishes one epoch, runs ranges on the caller (`JobSystem.cpp:156`), then waits until `remaining == 0` and `finished == workerCount` (`JobSystem.cpp:158-162`). Each worker adds one to `finished` after its own `RunRanges` returns (`JobSystem.cpp:84-85`), so the caller waits for every worker. `count == 0` returns before that wait (`JobSystem.cpp:133-135`).

Doodad skin enters this path when a batch has at least 8 instances (`Engine/Source/Model2/CM2SceneRender.cpp:794-795`). Exterior doodad cull enters at 32 or more (`WoW/Source/WorldClient/World.cpp:3965-3966`). The same wait sits on chunk visibility (`WoW/Source/WorldClient/MapChunk.cpp:578`), terrain owned fills (`WoW/Source/WorldClient/MapRenderChunk.cpp:333`), grass fills (`WoW/Source/WorldClient/DetailDoodad.cpp:730-732`), M2 animate roots (`Engine/Source/Model2/M2Scene.cpp:1728`), and nameplates when more than one unit is ready (`WorldFrame.cpp:1584-1590`).

**Reopen:** one-second sum of clocks blocked in `JobSystem.cpp:160`, greater than every campaign 1–6 zone on that line.

## ClipBufferUpdate

`CMapChunk::ClipBufferUpdate` (`WoW/Source/WorldClient/MapChunk.cpp:1302`) builds two 9-index outlines and passes each to `CWorldOcclusion::UpdateBufferChunk` with `m_height` and the hole mask (`MapChunk.cpp:1310-1314`, `MapChunk.cpp:1322-1327`). The first base is `136` when `s_activeWorldView.x > camTarget.x`, otherwise `0` (`MapChunk.cpp:1306-1308`). The second base is `8` when `s_activeWorldView.y > camTarget.y`, otherwise `0`, then steps by 17 (`MapChunk.cpp:1318-1326`). The function keeps no silhouette from the previous call.

`CWorldScene::UpdateOcclusion` (`WoW/Source/WorldClient/World.cpp:2592`) calls it for a hole-free chunk inside the near/far occlusion window (`World.cpp:2612-2616`) and for every chunk still on the holed list (`World.cpp:2623-2628`). `UpdateBufferChunk` returns immediately when culling is off or `camVec.z` is outside `±0.9` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:332-334`). Past that gate it projects the 9 vertices and writes the 384-wide clip buffer (`CWorldOcclusion.cpp:338-411`).

`CWorldOccluder::ClipBufferUpdate` (`CWorldOcclusion.cpp:606`) sends two points through `UpdateBuffer`. The reopen counter is `CMapChunk::ClipBufferUpdate`.

**Reopen:** one-second sum of `OsGetAsyncTimeClocks` spent in `MapChunk.cpp:1302` on the outdoor scene, greater than every campaign 1–6 zone on that line.
