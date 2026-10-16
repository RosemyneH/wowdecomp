# Campaign 0 edges — frame log

The one-second line is a compile-out counter dump through the existing console (`docs/perf-agent-plan.md`, Campaign 0). These are the cases that dump has to survive. A printed line of zeros is a successful sample. A missing line is either the flag compiled out or `ConsolePrintf` discarding the text.

## Threads

Two threads enter the hot functions.

| Thread | Where it starts | What it runs |
| --- | --- | --- |
| Client | `ClientMain` (`WoW/Source/Linux/WinMain.cpp:19`) calls `Job::Init` (`:25`) then `CommonMain` (`:26`). The fiber is `MainFiberProc` (`:31`) from `main` (`:35`). | `Job::ParallelFor` is main-thread only (`Engine/Source/Base/Job/JobSystem.h:19`). World draws are queued from `CGWorldFrame::OnFrameRender` (`WoW/Source/Ui/WorldFrame.cpp:2233`) into `RenderWorld` (`:2210`). |
| `wow-job` | `WorkerMain` (`Engine/Source/Base/Job/JobSystem.cpp:60`), named at `:62`, started from `Job::Init` (`:107`). | `RunRanges` (`:82`). The client thread runs the same function at `:156` before it waits at `:158`. A callback is on both threads until `remaining == 0`. |

`ParallelFor` returns immediately when `count == 0` (`JobSystem.cpp:133`) and does not wake the pool. `fn` must not call `Job::` or anything on `g_theGxDevicePtr` (`JobSystem.h:20`).

`AnimateModelRange` (`Engine/Source/Model2/M2Scene.cpp:247`, launched at `:1728`) and `PrepareNameplateRange` (`WoW/Source/Ui/WorldFrame.cpp:1557`, launched at `:1590`) are the same split. Campaigns 1–6 do not name them. No frame-log counter belongs in either body.

## Hot functions

Client-only sites run on the client thread. Split sites run on the client thread and on `wow-job` for the same `ParallelFor`.

| Function | Definition | Entered from | Threads |
| --- | --- | --- | --- |
| `DrawRigidDoodads` | `Engine/Source/Model2/CM2SceneRender.cpp:608` | `DrawBatchDoodad` `:717` | Client. Early `return 0` at `:609`. `BufLock` at `:645` and `:668`. `DrawInstanced` at `:691`. |
| `FillDoodadSkin` | `CM2SceneRender.cpp:598` | `ParallelFor` `:795` when `instances >= 8`. Direct call `:797` when `instances < 8`. | Split at `:795`. Client only at `:797`. Calls `s_transformVertices` at `:603`. |
| `CM2Model::TransformVertices_*` | `M2Model.cpp:3101`, `:3162`, `:3183`, `:3235`. AVX2 bodies `Engine/Source/Model2/M2SkinAvx2.cpp:110`, `:139`. Dispatch `M2Model.cpp:3871`. | `FillDoodadSkin` `:603` and `SetVertices` `:2717`, `:2722` | Split through `FillDoodadSkin`. Client only through `SetVertices`. |
| `CM2Model::SetVertices` | `M2Model.cpp:2700` | `CM2SceneRender.cpp:190` | Client. `BufLock` at `M2Model.cpp:2702`. |
| `CM2Shared::AllocInstances` | `Engine/Source/Model2/M2Shared.cpp:771` | `DrawBatchDoodad` `:711`, `RenderBatchShadowMap` `M2Model.cpp:2937` | Client. Calls `DestroyBuffers` at `M2Shared.cpp:779`. |
| `CM2Shared::DestroyBuffers` | `M2Shared.cpp:602` | `AllocInstances` `:779`. `CM2Cache::UpdateShared` `M2Cache.cpp:413` from `CM2Scene::SetTime` `M2Scene.cpp:51` and `AdvanceTime` `:59` | Client. |
| `CGxDeviceVulkan::Draw` | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1183` | `DrawInstanced` `:1499`, `DrawTerrainBatch` `:1538`, world and UI draw sites | Client. Returns at `:1194` when `!m_context`, `m_intF5C`, `!m_frameActive`, or `m_targetInvalid`. Further returns at `:1225`, `:1244`, `:1253`, `:1305`, `:1317`, `:1346`, `:1353`, `:1370`, `:1383`. `vkCmdDrawIndexed` `:1470`. `vkCmdDraw` `:1474`. |
| `IBuildUniforms` | `CGxDeviceVulkan.cpp:971` | `Draw` `:1359` | Client. |
| Uniform upload | `CGxDeviceVulkan.cpp:1385` | Inside the `memcmp` miss at `:1377` | Client. The ring-full return at `:1383` happens before the copy and before the descriptor bind. |
| `vkCmdBindDescriptorSets` | `CGxDeviceVulkan.cpp:1442` | Every `Draw` that reaches it | Client. |
| `DrawInstanced` | `CGxDeviceVulkan.cpp:1478` | `DrawRigidDoodads` `:691` | Client. Returns at `:1479` when `count == 0`. |
| `DrawTerrainBatch` | `CGxDeviceVulkan.cpp:1502` | `FlushMultiPass` `MapRenderChunk.cpp:2625` | Client. Returns `0` at `:1503` and `:1512`. |
| `QueueMultiPass` | `WoW/Source/WorldClient/MapRenderChunk.cpp:2526` | `CWorldScene::RenderChunksSinglePass` `World.cpp:3711` | Client. Special chunks call `RenderMultiPassAlpha` at `:2538` and never push `s_terrainBatch`. |
| `CaptureLights` | `MapRenderChunk.cpp:2463` | `QueueMultiPass` `:2548` | Client. Reads `g_theGxDevicePtr->m_lights` at `:2470`. |
| `FlushMultiPass` | `MapRenderChunk.cpp:2559` | `World.cpp:3728` | Client. Returns at `:2560` when `s_terrainBatch` is empty. Allocates `used` and `draws` at `:2572`. |
| `AcquireBuffers` | `WoW/Source/WorldClient/DetailDoodad.cpp:117` | `QueueBatchFill` `:667`, `CDetailDoodadInstBatch::Draw` `:885` | Client. Pool miss stores nulls at `:130`. |
| `FillVertices` | `DetailDoodad.cpp:340` | `FillMappedRange` `:617` via `ParallelFor` `:732`. `FillVerticesOnMain` `:632` from `Draw` `:893` | Split at `:732`. Client only at `:632`. |
| `FillLockedBufs` | `MapRenderChunk.cpp:160` | `FillOwnedRange` `:202` via `ParallelFor` `:333`. Overlap path `RenderPrepBufs` `:874` from `FlushOwnedFills` `:327` | Split at `:333`. Client only on the overlap path. Dual chunk: `CreateVertices` at `:165` and `:177` (base `+145`). `CreateIndices` at `:189` and `:193`. |
| `CMapChunk::CreateIndices` | `WoW/Source/WorldClient/MapChunk.cpp:882` | `FillLockedBufs` `:189`, `:193` | Same split as `FillLockedBufs`. |
| `RenderSetup` | `MapRenderChunk.cpp:910` | `World.cpp:2662`, `:2692`, `:3701` | Client. Calls `FlushOwnedFills` `:911` and `UseStreamingBufs` `:934` when `m_chunkBuf` is null. |
| `UseStreamingBufs` | `MapRenderChunk.cpp:889` | `RenderSetup` `:934` | Client. `BufStream` at `:899`. |
| `AllocBuf` | `MapRenderChunk.cpp:596` | `RenderSetup` `:993` | Client. `BufCreate` at `:653`. Returns null when the free list is empty (`:993` caller). |
| `CMapRenderChunk::UpdatePools` | `MapRenderChunk.cpp:805` | `CWorldScene::Render` `CWorldSceneRender.cpp:92` | Client. `FlushQueuedVisibility` `:806`. `PoolCreate` `:834` dereferences `g_theGxDevicePtr`. |
| `FlushQueuedVisibility` | `MapChunk.cpp:514` | `UpdatePools` `:806`, `PurgeChunkBufBlocks` `MapRenderChunk.cpp:671` | Client. Returns at `:515` when `s_visQueue` is empty. `ParallelFor` `RunChunkVisibility` at `:578`. |
| `RunChunkVisibility` | `MapChunk.cpp:452` | `FlushQueuedVisibility` `:578` | Split. |
| `CullDoodadsExterior` | `World.cpp:3924` | `CullChunks` `:4065` | Client. Returns at `:3960` when `s_doodadCulls` is empty. `ParallelFor` `:3966` when `size >= 32`, else a direct call `:3968`. |
| `CullDoodadRange` | `World.cpp:3907` | `:3966` or `:3968` | Split when `size >= 32`. Client only below that. |
| `CancelOwnedFill` | `MapRenderChunk.cpp:206` | `~CMapRenderChunk` `:786` | Client. Erases `s_ownedFills`. |
| `DropQueuedVisibility` | `MapChunk.cpp:390` | `CMapChunk::Purge` `:95` | Client. Erases `s_visQueue`. |
| `IBufLock` | `CGxDeviceVulkan.cpp:1589` | `BufLock` `:1674` | Client. `!m_context` uses emergency memory at `:1592` and does not call `vkAllocateMemory`. Stream path `:1605`. Failed `StreamLock` uses emergency memory at `:1606`. |
| `VkHeap::Alloc` / `vkAllocateMemory` | `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:7`, call `:51` | `CreateImage` `:1114` | Client. |
| `VkContext::CreateBuffer` / `vkAllocateMemory` | `VkContext.cpp:982`, call `:1003` | `AcquireBuffer` `:979`, `IPoolBuffer` `CGxDeviceVulkan.cpp:1546`, `BumpChunk` `VkContext.cpp:1024` and `:1034` | Client. Failure destroys the buffer and returns `0` at `:1004`. |

`FlushOwnedFills` (`MapRenderChunk.cpp:245`) is the client side of the terrain fill. It returns at `:253` when `g_theGxDevicePtr` is null and puts the pending list back.

## Log disabled

The flag is compile-out and default off. With it off, the hot functions keep the instructions they have now.

- No atomic, no `OsGetAsyncTimeClocks`, no `ConsoleWrite` / `ConsolePrintf` on those paths.
- `Job::Init` (`WinMain.cpp:25`) still runs. The flag does not own the pool.
- A disabled run produces no line. That silence is the off flag. It is a different outcome from an enabled second of zeros, and from `ConsolePrintf` dropping text.

## Log enabled, zero draws

An enabled second still prints one line when every counter is zero. The line carries explicit zeros for stream locks, uniform uploads, `vkAllocateMemory`, descriptor binds, terrain batch size, visibility flushes, and draw count. Zone times are the clock delta for zones that did not run.

These paths are real zeros. They still get a line:

- `Draw` never reaches `vkCmdDrawIndexed` (`CGxDeviceVulkan.cpp:1470`) or `vkCmdDraw` (`:1474`). The returns at `:1194` through `:1383` are not draws, not uploads, and not descriptor binds.
- `CGWorldFrame::OnWorldRender` returns at `WorldFrame.cpp:1780` when the window is empty, and at `:1784` when the viewport is empty.
- `FlushQueuedVisibility` returns at `MapChunk.cpp:515` on an empty queue.
- `CullDoodadsExterior` returns at `World.cpp:3960` on an empty cull list. `CullDoodadsInterior` (`World.cpp:3996`) never calls `ParallelFor`, so an interior second leaves the exterior cull counter at zero. `UpdatePools` still runs from `CWorldScene::Render` (`CWorldSceneRender.cpp:92`), so a visibility flush can still be non-zero indoors.
- `FlushMultiPass` returns at `MapRenderChunk.cpp:2560` when nothing was queued. A pure interior second can have terrain batch size zero.
- `s_enableShaders != 0` skips `DrawRigidDoodads` (`CM2SceneRender.cpp:717`). `FillDoodadSkin` does not run. The shader loop draws at `:768`, so uniform uploads and descriptor binds still move. The rigid stream-lock counter stays zero.
- Grass pool miss: `AcquireBuffers` stores nulls (`DetailDoodad.cpp:130`). `QueueBatchFill` returns at `:670`. `Draw` returns at `:888`. `FillVertices` does not run.
- `DrawRigidDoodads` returns at `:647` or `:670` when `BufLock` returns null. That attempt is not a successful stream lock.
- `World::GetFramerate` (`World.cpp:4809`) returns `1000.0` at `:4826` when the 30-sample average is under `0.001`. The log line does not divide by draw count or by that average.

A draw counter increments at `vkCmdDrawIndexed` / `vkCmdDraw`. An upload counter increments at the `memcpy` (`CGxDeviceVulkan.cpp:1385`). A descriptor counter increments at `vkCmdBindDescriptorSets` (`:1442`). Terrain batch size is the `draws.size()` passed into `DrawTerrainBatch` (`MapRenderChunk.cpp:2625`) during that second, sampled before `FlushMultiPass` finishes the batch. A stream-lock counter increments on the `StreamLock` success path (`CGxDeviceVulkan.cpp:1605`), including locks that later feed `wow-job` writers.

## Device not created yet

`g_theGxDevicePtr` is null before `CreateGxDevice` (`Engine/Source/Gx/Gx.cpp:230`), after a failed `DeviceCreate` (`:271`), and after `GxDevDestroy` (`:1194`). `CreateGxDevice` publishes the pointer at `:254` and then calls `DeviceCreate`. The constructor `CGxDeviceVulkan::CGxDeviceVulkan` (`CGxDeviceVulkan.cpp:120`) runs `DeviceCreatePools` and `DeviceCreateStreamBufs` at `:124` before `VkContext::CreateDevice`.

The dump and any zone timer on the client thread run without dereferencing `g_theGxDevicePtr`, `m_vk.m_device`, or `CGxDeviceVulkan`. Counters that live inside `Draw`, `IBufLock`, and `vkAllocateMemory` stay zero until those functions run.

- `Draw` returns at `CGxDeviceVulkan.cpp:1194` while `m_context` is still clear. `m_context` is set at `:250` after the swapchain exists.
- `IBufLock` takes emergency memory at `:1592` when `!m_context`.
- `FlushOwnedFills` returns at `MapRenderChunk.cpp:253` when the global is null.
- `CaptureLights` (`:2470`), `QueueMultiPass` (`:2543`), and `UpdatePools` (`:834`) dereference the global. The log does not call them, and does not wrap them in a new null check that changes their results.
- `OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`) loads `s_OsTimeMgr` with no null test. `OsTimeManagerCreate` (`:128`) is what publishes it. The one-second sample runs after that create. `OsTimeManagerDestroy` (`:133`) nulls it again.

On Windows, `ConsoleDeviceInitialize` can leave a D3D9 device in place (`WoW/Source/ConsoleDevice/ConsoleDevice.cpp:416`). `CGxDeviceVulkan` methods never run. Vulkan counters stay zero and the line still prints. The dump reads counters off the device object it was given. It does not cast a D3D device to `CGxDeviceVulkan`.

Login and glue frames can `Draw` UI before `CWorldScene::Render`. Those draws are draws. They are not a Campaign 0 scene sample.

## Multi-thread increments

`wow-job` and the client thread call `RunRanges` together (`JobSystem.cpp:82` and `:156`). Increments inside a split function are `std::atomic` `fetch_add` on a `uint64_t`.

Split functions:

- `FillDoodadSkin` (`CM2SceneRender.cpp:598`) and the `TransformVertices_*` call at `:603`
- `FillVertices` via `FillMappedRange` (`DetailDoodad.cpp:617`)
- `FillLockedBufs` / `CreateIndices` via `FillOwnedRange` (`MapRenderChunk.cpp:202`, `MapChunk.cpp:882`)
- `RunChunkVisibility` (`MapChunk.cpp:452`)
- `CullDoodadRange` (`World.cpp:3907`) when the list has at least 32 doodads

The one-second sample runs on the client thread after `ParallelFor` has returned. Workers are idle then (`remaining == 0` and `finished == workerCount`, `JobSystem.cpp:160`). The sample is an atomic exchange to zero so an increment that lands as the exchange runs is kept for the next second.

Workers do not call `ConsoleWrite`, `OsGetAsyncTimeClocks`, or `g_theGxDevicePtr`. `BufLock` / `IBufLock` stay on the client thread, before `ParallelFor` (`CM2SceneRender.cpp:777`, `DetailDoodad.cpp:691`, `MapRenderChunk.cpp:291`). Workers write the pointer they were handed.

`CancelOwnedFill` and `DropQueuedVisibility` mutate `s_ownedFills` and `s_visQueue` on the client thread. Workers do not touch those vectors. `FlushQueuedVisibility` swaps `s_visQueue` at `MapChunk.cpp:520` before the `ParallelFor`.

A direct call (`FillDoodadSkin` at `CM2SceneRender.cpp:797`, `CullDoodadRange` at `World.cpp:3968`, `FillVerticesOnMain` at `DetailDoodad.cpp:632`) is the client thread alone. The same atomic is used there.

## Counter overflow

Counters are `uint64_t`. The printed value is the exchange result for that second, formatted unsigned.

`uint32_t` wraps at 2^32. Stream bytes are the field that gets there: `StreamRingSize` is 64 MiB (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:135`) and rigid batches rewrite the mesh on every lock (`DrawRigidDoodads` `:644`). A cumulative byte counter, or a second that spans a hitch, wraps a 32-bit field. Draw counts and `vkAllocateMemory` counts fit in 32 bits for one second. They still use the same `uint64_t` so one dump format covers every field.

`OsGetAsyncTimeMs` (`TimeManager.cpp:73`) is `uint32_t` and wraps. `OsTimeManager::Snapshot` (`:68`) rounds the clock through `double`. The one-second gate subtracts two `OsGetAsyncTimeClocks` results as `uint64_t`, the same pattern as `ClientUpdateTaxiBenchmarkMode` (`WoW/Source/Client.cpp:2091`). On Linux, method 2 is `CLOCK_MONOTONIC` in nanoseconds (`TimeManager.cpp:59`).

Terrain batch size is a gauge taken from `draws.size()` at `MapRenderChunk.cpp:2625`. It is not added into the byte counter. Dual-chunk index fills (`FillLockedBufs` `:189` and `:193`) each count. A hole-free chunk writes 768 indices. A holed chunk writes fewer. The counter follows `CreateIndices`, including the `+145` half.

The job pool's `uint32_t` atomics (`JobSystem.cpp:27`) are the chunk index. They are not frame-log counters.

## Missing console

The dump calls `ConsoleWrite` (`WoW/Source/Console/ConsoleClient.cpp:873`).

`ConsoleWrite` writes stdout first when `s_consoleLogTee` is set (`:878`), then returns at `:895` when `ConsoleDeviceExists()` is false or `s_textFont` is null. `ConsolePrintf` (`:941`) returns before `ConsoleWrite` when the device is missing and the tee is off (`:942`), so a `ConsolePrintf` line is discarded in that state.

- `ConsoleDeviceExists` (`WoW/Source/ConsoleDevice/ConsoleDevice.cpp:80`) is `s_device != nullptr`. That pointer is assigned in `ConsoleDeviceInitialize` (`:401`). It is the Gx device.
- `IsConsoleEnabled` (`:86`) is `s_consoleAccessEnabled`, set from `CMD_CONSOLE` at `:325`. `ConsoleWrite` does not read it.
- `s_textFont` (`ConsoleClient.cpp:118`) is filled in `ConsoleScreenInitialize` (`:1299`), called from `InitializeEngineCallback` (`Client.cpp:1090`). That function calls `g_theGxDevicePtr->CapsWindowSize` (`ConsoleClient.cpp:1290`), so the font exists only after a device exists.
- `-consolelog` arms the tee in `InitializeGlobal` (`Client.cpp:1683`) via `ConsoleSetLogTee` (`ConsoleClient.cpp:864`), before the device exists. With the tee on, `ConsoleWrite` still prints when `s_device` or `s_textFont` is null.
- With neither a device nor the tee, `ConsolePrintf` drops the line and `ConsoleWrite` skips both sinks. Counters keep updating. The client thread does not enter `s_critsect` (`ConsoleClient.cpp:148`, entered at `:899`) and does not `SMemAlloc` a console line. The frame continues.

The dump runs once per second on the client thread. `wow-job` does not call `ConsoleWrite`. `s_critsect` serializes the on-screen list. The stdout path uses `flockfile` (`:883`) so a later writer cannot split a line.
