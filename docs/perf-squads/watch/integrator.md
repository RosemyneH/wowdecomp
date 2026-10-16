# Watch integrator

Watch-list slices. One writer at a time. The branch stays buildable by landing the steps below in order. A later step does not ride in an earlier commit.

## Merge order

1. **Frame log first.** Campaign 0 runs alone (`docs/perf-agent-plan.md:41`). This squad merges after that one-second line exists and ranks the item above campaigns 1–6 (`docs/perf-agent-plan.md:50`, `docs/perf-agent-plan.md:162`). Timers are `OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`).
2. **Local files, one slice each, after the owner of that file has closed.**
   - Portal cover. `CMapObj::RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1165`). The nested loop is pending views × covered rects (`MapObj.cpp:1316`, inner scan `MapObj.cpp:1320`, overlap test `MapObj.cpp:1323`). Same predicate. No campaign owns `MapObj.cpp`.
   - Nameplates. `MarkSerialNameplates` (`WoW/Source/Ui/WorldFrame.cpp:1532`). The pair loop is `WorldFrame.cpp:1541`, the model-pointer compare is `WorldFrame.cpp:1548`, the call is `WorldFrame.cpp:1576`. Leave `Job::ParallelFor` at `WorldFrame.cpp:1590` for step 3.
   - Occlusion silhouettes. `CMapChunk::ClipBufferUpdate` (`WoW/Source/WorldClient/MapChunk.cpp:1302`) rebuilds two 9-vertex outlines (`MapChunk.cpp:1310`, `MapChunk.cpp:1322`). Callers are `CWorldScene::UpdateOcclusion` (`WoW/Source/WorldClient/World.cpp:2616`, `World.cpp:2628`, `World.cpp:2636`). Campaign 5 owns `MapChunk` visibility and doodad-cull scratch in `World` (`docs/perf-agent-plan.md:47`). This slice waits until campaign 5 closes, and until the log shows the rebuild.
3. **`JobSystem.cpp` after the terrain and skin jobs.** Those campaigns call `Job::ParallelFor` (`Engine/Source/Base/Job/JobSystem.cpp:132`). The caller waits until every worker finishes (`JobSystem.cpp:158-162`). Skin is group A, campaign 1 (`docs/perf-agent-plan.md:45`): `FillDoodadSkin` at `Engine/Source/Model2/CM2SceneRender.cpp:795`. Terrain is group D, campaigns 3 then 4 (`docs/perf-agent-plan.md:48`): `FlushOwnedFills` at `WoW/Source/WorldClient/MapRenderChunk.cpp:333`, grass `PrepareGrassFills` at `WoW/Source/WorldClient/DetailDoodad.cpp:732`. Land this file alone once both groups have closed. The gate is the plan line: only if campaign 1 still spends time inside small doodad jobs (`docs/perf-agent-plan.md:170`).
4. **`stpl.h` alone, last, and only if the counters say grows dominate.** `TSFixedArray::ReallocData` (`Engine/Source/Storm/H/stpl.h:749`) always passes `SMEM_FLAG_NOREALLOC` (`stpl.h:757`), so every grow allocs and copy-constructs. `TSGrowableArray` grows through that function (`stpl.h:915`, `stpl.h:939`, `stpl.h:1001`). The header is on the device and on every campaign TU (`Engine/Source/Gx/CGxDevice/CGxDevice.h:17`, `Engine/Source/Model2/M2Scene.h:10`, `WoW/Source/WorldClient/MapRenderChunk.h:9`, `WoW/Source/WorldClient/DetailDoodad.h:7`, `WoW/Source/WorldClient/MapChunk.h:13`, `WoW/Source/WorldClient/World.h:12`, `WoW/Source/WorldClient/MapObj.cpp:27`). A trivial-type `realloc` stays a separate change with a type-trait gate (`docs/perf-agent-plan.md:169`). The `// OFFSET:` list on `ReallocData` stays behavior-compatible (`stpl.h:747`).

## `JobSystem.cpp` callers that must still build

Step 3 changes the barrier for every caller. Terrain and skin are the campaigns that share the file. These other calls stay source-compatible when that commit lands:

| Caller | Site | Campaign |
| --- | --- | --- |
| `FillDoodadSkin` | `CM2SceneRender.cpp:795` | 1 skin |
| `FlushOwnedFills` | `MapRenderChunk.cpp:333` | 3, then 4 terrain |
| `PrepareGrassFills` | `DetailDoodad.cpp:732` | 4 grass |
| `CMapChunk::FlushQueuedVisibility` | `MapChunk.cpp:578` | 5 |
| `CWorldScene::CullDoodadsExterior` | `World.cpp:3966` | 5 |
| `CM2Scene::Animate` | `Engine/Source/Model2/M2Scene.cpp:1728` | M2 animate |
| `PostAnimateVisibleUnits` | `WorldFrame.cpp:1590` | watch nameplates |

`ParallelFor` is declared at `Engine/Source/Base/Job/JobSystem.h:21`. Step 3 edits the `.cpp`. It does not edit the call sites in the table.

## Review fixes

Apply a review note inside the step that owns the lines.

- A portal, nameplate, or silhouette fix lands in step 2, in that file only.
- A shorter barrier lands in step 3, after terrain and skin have closed.
- A grow path lands in step 4, in `stpl.h` only, when the frame-log counters say grows dominate.

`ReallocAndClearData` (`stpl.h:732`) passes flag `0` (`stpl.h:741`). It stays out of the `ReallocData` commit.
