# Interior UI — Integrator

Apply Faith, Edges, and Leak fixes for this slice, then leave the branch buildable. The slice is one function. It shares no file with the other nine squads.

## This slice

`MarkSerialNameplates` in `WoW/Source/Ui/WorldFrame.cpp:1532`.

The function is the whole edit. Two loops:

- `WorldFrame.cpp:1535` marks a unit deferred when `NameplateModelStaysSerial` (`WorldFrame.cpp:1516`) says the model stays on this thread.
- `WorldFrame.cpp:1541` walks every later unit and marks both deferred when `m_worldModel` pointers match. That inner walk is the O(n²) the watch list names (`docs/perf-agent-plan.md:168`).

The only caller is `PostAnimateVisibleUnits` (`WorldFrame.cpp:1576`). The list it fills is `s_visibleUnits` (`WorldFrame.cpp:1497`), cleared at `WorldFrame.cpp:1574`. A grouping table that replaces the inner walk lives next to that vector, in this file. It keeps capacity across the `clear()` at `WorldFrame.cpp:1574`. It does not allocate on the steady crowd frame after warmup, and it does not store a model pointer past the call.

`defer` is the contract the rest of `PostAnimateVisibleUnits` already reads (`WorldFrame.cpp:1581`, `WorldFrame.cpp:1596`). A review fix that changes which units get `defer == 1` fails Faith: same shared `m_worldModel` still means both units deferred, including a null model and a model that `NameplateModelStaysSerial` already marked.

Do not edit `Job::ParallelFor` at `WorldFrame.cpp:1590`. That call stays. The barrier it waits on is `ParallelFor` in `Engine/Source/Base/Job/JobSystem.cpp:132`, which belongs to watch.

## Later slice

Portal cover stays out of this branch. `CMapObj::RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1165`) builds `s_pendingPortalViews` and `s_coveredRects` (`MapObj.cpp:1194`). After the portal walk, `MapObj.cpp:1316` scans pending views × covered rects and pushes a view only when no rect overlaps it (`MapObj.cpp:1323`, `MapObj.cpp:1328`). That is the watch-list loop (`docs/perf-agent-plan.md:167`).

No review fix in this slice edits `MapObj.cpp`. A portal speedup opens only after the nameplate deferred set matches, and only in that file.

## The other nine squads

None of them name `WorldFrame.cpp` or `MapObj.cpp`.

| Squad | Owns | File |
| --- | --- | --- |
| log | Frame-log hooks on the Campaign 0 sites: outdoor, M2, terrain, grass, Vulkan alloc / uniform / descriptor (`docs/perf-agent-plan.md:60`). Not nameplates. | Those squads’ functions below, plus the new log module. |
| m2-rigid | `DrawRigidDoodads` | `Engine/Source/Model2/CM2SceneRender.cpp:608` |
| m2-skin | `FillDoodadSkin`, `CM2Model::SetVertices`, `CM2Shared::AllocInstances`, `CM2Shared::DestroyBuffers` | `CM2SceneRender.cpp:598`, `Engine/Source/Model2/M2Model.cpp:2700`, `Engine/Source/Model2/M2Shared.cpp:771`, `M2Shared.cpp:602` |
| uniforms | `CGxDeviceVulkan::Draw`, `CGxDeviceVulkan::IBuildUniforms` | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1183`, `CGxDeviceVulkan.cpp:971` |
| vulkan | `CGxDeviceVulkan::IBufLock`, `VkContext::CreateBuffer`, `VkHeap` | `CGxDeviceVulkan.cpp:1589`, `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:982`, `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:53` |
| terrain | `QueueMultiPass`, `FlushMultiPass`, `CaptureLights` | `WoW/Source/WorldClient/MapRenderChunk.cpp:2526`, `MapRenderChunk.cpp:2559`, `MapRenderChunk.cpp:2463` |
| remesh | `AcquireBuffers`, `UseStreamingBufs`, `AllocBuf`, `CMapChunk::CreateIndices` | `WoW/Source/WorldClient/DetailDoodad.cpp:117`, `MapRenderChunk.cpp:889`, `MapRenderChunk.cpp:596`, `WoW/Source/WorldClient/MapChunk.cpp:302` |
| scratch | `FlushQueuedVisibility`, `CullDoodadsExterior`, `DropQueuedVisibility`, `CancelOwnedFill` | `MapChunk.cpp:514`, `WoW/Source/WorldClient/World.cpp:3924`, `MapChunk.cpp:390`, `MapRenderChunk.cpp:206` |
| watch | `TSFixedArray::ReallocData`, `ParallelFor`, `CMapChunk::ClipBufferUpdate` | `Engine/Source/Storm/H/stpl.h:749`, `JobSystem.cpp:132`, `MapChunk.cpp:1302` |

Shared files among those nine stay shared among those nine: `CM2SceneRender.cpp` (m2-rigid, m2-skin), `CGxDeviceVulkan.cpp` (uniforms, vulkan), `MapRenderChunk.cpp` (terrain, remesh, scratch), `MapChunk.cpp` (remesh, scratch, watch). `stpl.h:757` passes `SMEM_FLAG_NOREALLOC` and merges alone, last, under watch. This slice does not touch it.

## Merge

Land the nameplate diff on its own. It touches `MarkSerialNameplates` and the file-scope list in `WorldFrame.cpp` only, so it has no hunk against log, m2-rigid, m2-skin, uniforms, vulkan, terrain, remesh, scratch, or watch.

Reject a review fix that reaches `JobSystem.cpp`, `stpl.h`, `MapObj.cpp`, or any function in the table above. That fix belongs to that squad, or to the portal slice.
