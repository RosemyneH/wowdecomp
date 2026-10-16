# Frame-log integrator

Campaign 0 lands the counters. The other squads rewrite the functions those counters sit in, on separate branches. This is the merge contract. A rewrite that still does the work and loses the counter is a dropped hook.

Base is `main` at `94c1e172` (`Instance rigid doodads and multi-draw terrain that shares a texture.`). That commit already rewrote the shared hot files together. The working tree on `main` is clean. The older worktrees (`detail-doodad-fill`, `frame-cpu-worldframe`, `job-system`, `m2-animate-jobs`, `worker-2-terrain-chunks`) are ancestors of this commit. Branch from `94c1e172` after the log commit, not from those worktrees.

## Frozen line

The printer lives in the frame-log module:

- `Engine/Source/Gx/FrameLog.h`
- `Engine/Source/Gx/FrameLog.cpp`

`Engine/Source/Gx/CMakeLists.txt` globs `*.cpp`. No squad edits a `CMakeLists.txt` for this campaign. No second printer, no second atomic struct, no second compile-out flag.

The flag is `WOW_FRAME_LOG`, default off. With the flag off the calls compile to empty. They stay in the source anyway. One include of `FrameLog.h` per hot file, added by log. A later squad keeps that include.

Field order is frozen once the log squad prints the first one-second line. Names, in this order:

| Field | Counts |
| --- | --- |
| zone times | `OsGetAsyncTimeClocks` around the zones below |
| alloc counts | `new VkBufferAlloc` and the flush vectors Campaigns 3 and 5 allocate |
| stream bytes | bytes returned from `CGxDevice::BufLock` |
| `vkAllocateMemory` count | every successful `vkAllocateMemory` |
| uniform uploads | memcpy into the uniform ring |
| descriptor binds | `vkCmdBindDescriptorSets` that actually run |
| terrain batch size | chunks in the flush, and capture bytes |
| draw count | `CGxDeviceVulkan::Draw` entries that issue a draw |
| visibility flushes | `CMapChunk::FlushQueuedVisibility` runs |

Later squads keep these names. A new sample is a field appended at the end after Campaign 0 is closed. `World::GetFramerate` stays the 30-sample average it is.

The one-second dump is one `ConsoleWrite` from `CWorld::Update`, beside the `profTimes` store (`World.cpp` around the `OFFSET: 0x007831A0` function). That is the only edit log makes in that function.

## How a hook survives a rewrite

Each hook is one call on the line that still does the work.

- The operation moved: the call moves with it.
- The operation is gone: the call goes with it, and the frozen field prints zero. A rigid batch that no longer stream-locks must not keep adding mesh bytes on the matrix path.
- The operation grew a new site: that site uses the same call. A new `vkAllocateMemory` in the device-local upload path increments `vkAllocateMemory` count.

Resolve a conflict by keeping the call and the new body. Taking either side of the file whole drops the other.

## Merge order

Log merges first, alone. Every other branch rebases onto that commit before it merges. A branch cut from `94c1e172` with no log commit in its history does not merge.

After log, three streams share no files with each other. Inside a stream the order is fixed. Merge one branch at a time.

| Order | Squad | Stream |
| --- | --- | --- |
| 1 | log | counters |
| 2 | m2-rigid | M2 |
| 3 | m2-skin | M2 |
| 4 | uniforms | device |
| 5 | vulkan | device |
| 6 | scratch | world CPU |
| 7 | terrain | world CPU |
| 8 | remesh | world CPU |
| 9 | interior-ui | only if the closed log ranks portals or nameplates |

`m2-rigid` before `m2-skin` because both edit `DrawBatchDoodad`. `uniforms` before `vulkan` because both edit `CGxDeviceVulkan::Draw`. `scratch` before `terrain` before `remesh` because `remesh` rewrites callers of the scratch arena, and Campaign 4 starts when Campaign 3 is closed.

## Files each squad owns

A squad edits only the functions in its rows. A header changes only when a signature in that row changes. Prefer the current signatures so `World.cpp` call sites stay put.

### log

Owns the module above, the include in each hot file, and one call at each anchor. The function body around the call belongs to the squad named on that row.

| Anchor | File | Function |
| --- | --- | --- |
| zone | `WoW/Source/WorldClient/World.cpp` | `CWorld::Update` (dump only) |
| zone | `WoW/Source/WorldClient/MapChunk.cpp` | `CMapChunk::FlushQueuedVisibility` |
| zone | `WoW/Source/WorldClient/World.cpp` | `CWorldScene::CullDoodadsExterior` |
| zone | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `CMapRenderChunk::FlushMultiPass` |
| zone | `Engine/Source/Model2/CM2SceneRender.cpp` | `DrawRigidDoodads`, `FillDoodadSkin` |
| stream bytes | `Engine/Source/Gx/CGxDevice/CGxDevice.cpp` | `CGxDevice::BufLock` |
| alloc counts | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | each `new VkBufferAlloc` in `IBufLock` |
| alloc counts | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `FlushMultiPass` (`used`, `draws`) |
| alloc counts | `WoW/Source/WorldClient/MapChunk.cpp` | `FlushQueuedVisibility` (`jobs`, `liquids`) |
| `vkAllocateMemory` | `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp` | `VkHeap::Alloc`, `VkContext::CreateBuffer` |
| uniform uploads | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | the memcpy in `Draw` |
| descriptor binds | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `vkCmdBindDescriptorSets` in `Draw` |
| terrain batch size | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `FlushMultiPass` |
| draw count | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `Draw`, on the path that issues `vkCmdDraw*` |
| visibility flushes | `WoW/Source/WorldClient/MapChunk.cpp` | `FlushQueuedVisibility` |
| grass miss | `WoW/Source/WorldClient/DetailDoodad.cpp` | `AcquireBuffers` null path, `FillVertices` |

`DrawInstanced` and `DrawTerrainBatch` call `Draw`, so their uploads and binds are the `Draw` counters. No second increment in those two wrappers.

Stream bytes are counted in `CGxDevice::BufLock` only. `IBufLock` does not count them again (`BufLock` already called it).

### m2-rigid

| File | Functions |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp` | `DrawRigidDoodads`; the early-out call in `DrawBatchDoodad` |

The early-out is the `DrawRigidDoodads(...)` call and its `return` (currently the `s_enableShaders` test just after `AllocInstances`). `AllocInstances` stays above that return. The shader-on block and the CPU-skin `else` stay as they are.

Section meshes bind the existing `CM2Shared::m_vertexBuf` / `m_indexBuf`. Any extra rigid cache is file-scope in `CM2SceneRender.cpp`, next to `DrawRigidDoodads`.

### m2-skin

| File | Functions |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp` | `DoodadSkin`, `FillDoodadSkin`, the CPU-skin `else` in `DrawBatchDoodad` |
| `Engine/Source/Model2/M2Model.cpp` | `CM2Model::SetVertices`, `TransformVertices_cpp`, `TransformVertices_sse` |
| `Engine/Source/Model2/M2SkinAvx2.cpp` | `CM2Model::TransformVertices_avx2` |
| `Engine/Source/Model2/M2Shared.cpp` | `CM2Shared::AllocInstances` |

Skin counters stay on `FillDoodadSkin` and `CM2Model::SetVertices`. The three `TransformVertices_*` bodies have no counter of their own.

`AllocInstances` is the hysteresis edit (stop calling `DestroyBuffers` on a grow). `CM2Shared::DestroyBuffers` itself stays. `M2Cache.cpp` still calls it on teardown. `M2Shared.h` stays. The shader-on block in `DrawBatchDoodad` stays.

### uniforms

| File | Functions |
| --- | --- |
| `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `IBuildUniforms`; in `Draw`, the block from `VkDrawUniforms uniforms` through `vkCmdBindDescriptorSets` |
| `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h` | dirty-bit members on the device |
| `Engine/Source/Gx/Gx.cpp` | `GxXformSet` |
| `Engine/Source/Gx/CGxDevice/CGxDevice.cpp` | `RsSet` overloads, `LightEnable`, `LightSet` |

`VkDrawUniforms` in `CGxDeviceVulkan.h` stays 768 bytes and keeps its field order. Dirty bits live on `CGxDeviceVulkan`. `CGxBuf::m_dirty` is the buffer flag; it is not the uniform dirty bit.

The uniform-upload call stays on the memcpy that still happens. The descriptor-bind call stays inside the new “set or dynamic offset changed” branch, so the field counts binds that run.

### vulkan

| File | Functions |
| --- | --- |
| `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp` | `VkHeap::Alloc`, `VkHeap::Free`, `CreateBuffer`, `AcquireBuffer`, `BumpChunk`, `StreamLock`, `DestroyBuffer` |
| `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h` | `VkHeap`, `VkBufferAlloc` |
| `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `IBufLock`, `IBufUnlock`, `BufData`, `IPoolBuffer`, `IReleasePool`; the `VkBufferAlloc*` casts in `Draw` |
| `Engine/Source/Gx/CGxPool.h` | embedded borrowed view |
| `Engine/Source/Gx/CGxBuf.h` | embedded borrowed view |

`StreamRingSize` stays 64 MiB. `GpuDrawItem` stays 448 bytes. Every remaining `vkAllocateMemory` keeps the count call, including a device-local upload. Every remaining `new VkBufferAlloc` keeps the alloc-count call.

In `Draw`, vulkan’s lines are the pool casts (vertex bindings, then the index buffer). The uniform block between them is the uniforms squad’s.

### scratch

| File | Functions |
| --- | --- |
| `WoW/Source/WorldClient/MapChunk.cpp` | `DropQueuedVisibility`, `s_visQueue`, `EnqueueVisibility`, `FlushQueuedVisibility`, the `Vis*` types above them |
| `WoW/Source/WorldClient/MapRenderChunk.cpp` | `s_ownedFills`, `CancelOwnedFill`, `ClearOwnedFills`, `QueueOwnedFill`, `FlushOwnedFills` |
| `WoW/Source/WorldClient/World.cpp` | `DoodadCull`, `s_doodadCulls`, `CullDoodadRange`, `CWorldScene::CullDoodadsExterior` |
| `WoW/Source/WorldClient/World.h` | `CullDoodadsExterior`, only if the signature changes |

`CMapChunk::ClipBufferUpdate` stays (occlusion watch item). `CWorldScene::CullDoodadsInterior` stays. `CWorld::Update` stays. The chunk layer loop in `World.cpp` (`RenderSetup`, `QueueMultiPass`, `FlushMultiPass`) stays.

### terrain

| File | Functions |
| --- | --- |
| `WoW/Source/WorldClient/MapRenderChunk.cpp` | `TerrainCapture`, `s_terrainBatch`, `CaptureLights`, `ApplyChunkFog`, `FillTerrainDraw`, `DrawTerrainChunk`, `QueueMultiPass`, `FlushMultiPass` |
| `WoW/Source/WorldClient/MapRenderChunk.h` | `QueueMultiPass`, `FlushMultiPass`, only if a signature changes |

`GxTerrainDraw` in `CGxDevice.h` stays. `CGxDeviceVulkan::DrawTerrainBatch` stays. The `World.cpp` calls of `QueueMultiPass` and `FlushMultiPass` stay. Batch-count and capture-byte calls stay inside `FlushMultiPass` and use the vector after the slim.

### remesh

| File | Functions |
| --- | --- |
| `WoW/Source/WorldClient/DetailDoodad.cpp` | `AcquireBuffers`, `FillVertices`, `QueueBatchFill`, the other `AcquireBuffers` fill path |
| `WoW/Source/WorldClient/DetailDoodad.h` | only if one of those signatures changes |
| `WoW/Source/WorldClient/MapRenderChunk.cpp` | `AllocBuf`; pool sizing in `UpdatePools`; `RenderPrepBufs`; `UseStreamingBufs`; the null-`m_chunkBuf` branch of `RenderSetup`; `s_bufList` use in `RenderPrep` |
| `WoW/Source/WorldClient/MapChunk.cpp` | both `CMapChunk::CreateIndices` overloads |

These calls stay, because the scratch counters live inside the callees: `FlushOwnedFills` at the start of `RenderSetup`, `QueueOwnedFill` in `RenderPrep`, and `FlushQueuedVisibility` / `ClearOwnedFills` / `FlushOwnedFills` at the start of `UpdatePools`.

### interior-ui

Campaign 0 does not hook these files. Merging this squad does not add a frame-log call.

| File | Functions |
| --- | --- |
| `WoW/Source/WorldClient/MapObj.cpp` | `CMapObj::RenderThruPortals` |
| `WoW/Source/Ui/WorldFrame.cpp` | `MarkSerialNameplates` |

## Conflicts already in the tree

`94c1e172` is one commit across `CGxDevice.cpp`, `CGxDevice.h`, `CGxDeviceVulkan.cpp`, `CGxDeviceVulkan.h`, `VkContext.cpp`, `VkContext.h`, `CM2SceneRender.cpp`, `MapRenderChunk.cpp`, `MapRenderChunk.h`, `World.cpp`, `Ffp.vert`, `Shaders/Generated/Ffp.vert.inc`, and `Tools/spirv-pin-compare.py`. The squads below split that commit’s files. The shader, the generated include, and `spirv-pin-compare.py` stay as that commit left them. No squad edits them.

Same function, two squads:

| File | Function | Split already in the body |
| --- | --- | --- |
| `CM2SceneRender.cpp` | `DrawBatchDoodad` | `AllocInstances`, then the rigid early-out (`m2-rigid`), then the shader-on block (frozen), then the CPU-skin `else` (`m2-skin`). `FillDoodadSkin` is the skin entry just above `DrawRigidDoodads`. |
| `CGxDeviceVulkan.cpp` | `Draw` | `VkBufferAlloc*` cast while binding vertex buffers (`vulkan`), then `IBuildUniforms` through `vkCmdBindDescriptorSets` (`uniforms`, and the upload and bind counters), then the index-buffer `VkBufferAlloc*` cast (`vulkan`). `DrawInstanced` and `DrawTerrainBatch` both fall into this function. |
| `MapRenderChunk.cpp` | `RenderSetup` | `FlushOwnedFills()` (`scratch`) and, when `m_chunkBuf` is null, `UseStreamingBufs()` (`remesh`). |
| `MapRenderChunk.cpp` | `UpdatePools` | `FlushQueuedVisibility`, `ClearOwnedFills`, `FlushOwnedFills` (`scratch`), then the far-clip radius and the `768 * count` index pool (`remesh`). |
| `MapRenderChunk.cpp` | `RenderPrep` | `AllocBuf` and `s_bufList.LinkToHead` (`remesh`), then `QueueOwnedFill` (`scratch`). |
| `MapRenderChunk.cpp` | `~CMapRenderChunk` | calls `CancelOwnedFill` (`scratch`). The destructor body stays. |

Same file, different functions. Git will merge these only while each squad stays inside its functions:

| File | Squads | What collides if a squad reformats the file |
| --- | --- | --- |
| `CGxDeviceVulkan.cpp` | uniforms, vulkan, log | uniform block vs `IBufLock` / pool casts. `IBufLock` still has two `new VkBufferAlloc` sites. |
| `CGxDeviceVulkan.h` | uniforms, vulkan | dirty bits vs anything vulkan adds. `VkDrawUniforms` is in this header and stays. |
| `CGxDevice.cpp` | log, uniforms | `BufLock` stream-byte call vs `RsSet` / `LightEnable` / `LightSet`. |
| `CGxDevice.h` | nobody | `GxTerrainDraw` is the 448-byte CPU record. Terrain slims `TerrainCapture` in `MapRenderChunk.cpp`, not this struct. |
| `VkContext.cpp` | log, vulkan | `vkAllocateMemory` in `VkHeap::Alloc` and again in `CreateBuffer`. |
| `MapRenderChunk.cpp` | scratch, terrain, remesh, log | owned-fill arena at the top, streaming and pools in the middle, `TerrainCapture` / `FlushMultiPass` at the bottom. |
| `MapRenderChunk.h` | terrain, remesh | `QueueMultiPass` / `FlushMultiPass` and `UseStreamingBufs` / `AllocBuf` / `RenderSetup` are the same class. |
| `MapChunk.cpp` | scratch, remesh | `CreateIndices(char*)`, then the visibility queue and `FlushQueuedVisibility`, then `CreateIndices(uint16_t*)`, then `ClipBufferUpdate`. |
| `World.cpp` | log, scratch | dump in `CWorld::Update`; doodad arena at `CullDoodadsExterior`. The layer loop that calls `RenderSetup` and `QueueMultiPass` sits between them and stays. |
| `CGxPool.h`, `CGxBuf.h` | vulkan | `m_apiSpecific` is the heap `VkBufferAlloc` `IBufLock` and `Draw` both cast. Embedding the view touches every lock and both casts together. |

`DetailDoodad.cpp` is remesh plus the grass counters. No other squad has a function there.

`MapObj.cpp` and `WorldFrame.cpp` do not share a function with Campaigns 1–6. Interior and crowd scenes still produce the frozen line through the shared draw, lock, and visibility counters.
