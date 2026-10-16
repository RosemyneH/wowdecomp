# Campaign 0 counters

Frozen names for the one-second frame line. Later squads keep these names. The numbers below are the current code. `docs/perf-agent-plan.md` is the map; when it disagrees with a function, the function is the counter.

Gate: `WOWDECOMP_FRAME_LOG`, compile-out, default undefined. With the flag off, the sites do not read a clock and do not add. Each counter is a `std::atomic<uint64_t>` added with relaxed order. `CDetailDoodadInstBatch::FillVertices` runs on job threads (`DetailDoodad.cpp:617` via `FillMappedRange`). The other sites run on the thread that called them.

One line per elapsed second of `OsGetAsyncClocksPerSecond()` clocks (`Engine/Source/Os/Linux/TimeManager.cpp:78`). The dump exchanges every counter to 0 and prints the captured totals. Sums, not peaks.

`zone_draw` sits inside `zone_m2`, `zone_terrain`, and `zone_grass`. `rigid_stream_bytes` is the `DrawRigidDoodads` subset of `stream_bytes`. Leave them as separate fields.

## Line

```
frame zone_m2=%llu zone_draw=%llu zone_terrain=%llu zone_grass=%llu zone_vis=%llu zone_cull=%llu stream_bytes=%llu rigid_stream_bytes=%llu skin_locks=%llu uniform_uploads=%llu descriptor_binds=%llu draws=%llu terrain_batch_size=%llu terrain_batches=%llu vis_flush_allocs=%llu doodad_cull_allocs=%llu terrain_flush_allocs=%llu stream_lock_news=%llu vk_allocs=%llu grass_fill_vertices=%llu use_streaming_bufs=%llu destroy_buffers=%llu
```

Print with `ConsoleWrite` (`WoW/Source/Console/ConsoleClient.cpp:873`) at the end of `CGWorldFrame::RenderWorld` (`WoW/Source/Ui/WorldFrame.cpp:1910`), after `CVehiclePassenger_C::PostRenderUpdateAll`. M2 passes, terrain, and grass have already run. Stamp the first frame, then print once the clock delta reaches one second.

## Clock

`OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`).

| Timing method | Clock | Unit of one clock |
| --- | --- | --- |
| `SystemMethod2` | `CLOCK_MONOTONIC` | nanosecond (`ReadClock` divisor `1000000000`, line 61) |
| otherwise | `CLOCK_MONOTONIC_COARSE` | millisecond (line 64) |

`OsTimeManager::Calibrate` (`TimeManager.cpp:16`) returns `SystemMethod2` when `clock_getres(CLOCK_MONOTONIC)` succeeds. Zone fields store the raw delta (`end - start`). Divide by `OsGetAsyncClocksPerSecond()` to get seconds. The same function is the one-second gate, so coarse mode still prints about once per second.

A zone reads the clock on entry and on every return, then adds the delta. `zone_grass` stops after `World.cpp:3614`. `RenderOccluders`, `RenderOcclusionVolumes`, and `RenderDebugFrustum` (`World.cpp:3616`) stay outside it.

## Counters

| Name | Unit | File | Function | Line | Adds |
| --- | --- | --- | --- | --- | --- |
| `zone_m2` | clocks | `Engine/Source/Model2/M2Scene.cpp` | `CM2Scene::Draw` | 2208 | delta of the whole function |
| `zone_draw` | clocks | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `CGxDeviceVulkan::Draw` | 1183 | delta of the whole function |
| `zone_terrain` | clocks | `WoW/Source/WorldClient/World.cpp` | `CWorldScene::RenderChunks` | 3753 | delta of the whole function |
| `zone_grass` | clocks | `WoW/Source/WorldClient/World.cpp` | `CWorldScene::RenderDetailDoodads` | 3573–3614 | delta through `RsPop` |
| `zone_vis` | clocks | `WoW/Source/WorldClient/MapChunk.cpp` | `CMapChunk::FlushQueuedVisibility` | 514 | delta of the whole function |
| `zone_cull` | clocks | `WoW/Source/WorldClient/World.cpp` | `CWorldScene::CullDoodadsExterior` | 3924 | delta of the whole function |
| `stream_bytes` | bytes | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `CGxDeviceVulkan::IBufLock` | 1605 | `buf->m_size` |
| `rigid_stream_bytes` | bytes | `Engine/Source/Model2/CM2SceneRender.cpp` | `DrawRigidDoodads` | 645, 668 | locked `m_size` |
| `skin_locks` | calls | `Engine/Source/Model2/CM2SceneRender.cpp` | `CM2SceneRender::DrawBatchDoodad` | 777 | 1 |
| `skin_locks` | calls | `Engine/Source/Model2/M2Model.cpp` | `CM2Model::SetVertices` | 2702 | 1 |
| `skin_locks` | calls | `Engine/Source/Model2/M2Model.cpp` | `CM2Model::SetVerticesMultiSample` | 4521 | 1 |
| `uniform_uploads` | uploads | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `CGxDeviceVulkan::Draw` | 1385 | 1 |
| `descriptor_binds` | binds | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `CGxDeviceVulkan::Draw` | 1442 | 1 |
| `draws` | draws | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `CGxDeviceVulkan::Draw` | 1467, 1470, 1474 | 1 |
| `terrain_batch_size` | chunks | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `CMapRenderChunk::FlushMultiPass` | 2572 | `s_terrainBatch.size()` |
| `terrain_batches` | batches | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `CMapRenderChunk::FlushMultiPass` | 2625 | 1 |
| `vis_flush_allocs` | allocs | `WoW/Source/WorldClient/MapChunk.cpp` | `CMapChunk::FlushQueuedVisibility` | 524, 549 | 1 |
| `doodad_cull_allocs` | allocs | `WoW/Source/WorldClient/World.cpp` | `CWorldScene::CullDoodadsExterior` | 3957 | 1 on growth |
| `terrain_flush_allocs` | allocs | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `CMapRenderChunk::FlushMultiPass` | 2572, 2603 | 1 |
| `stream_lock_news` | allocs | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp` | `CGxDeviceVulkan::IBufLock` | 1619, 1634 | 1 |
| `vk_allocs` | allocs | `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp` | `VkHeap::Alloc` | 51 | 1 on `VK_SUCCESS` |
| `vk_allocs` | allocs | `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp` | `VkContext::CreateBuffer` | 1003 | 1 on `VK_SUCCESS` |
| `grass_fill_vertices` | calls | `WoW/Source/WorldClient/DetailDoodad.cpp` | `CDetailDoodadInstBatch::FillVertices` | 340 | 1 |
| `use_streaming_bufs` | calls | `WoW/Source/WorldClient/MapRenderChunk.cpp` | `CMapRenderChunk::UseStreamingBufs` | 889 | 1 |
| `destroy_buffers` | calls | `Engine/Source/Model2/M2Shared.cpp` | `CM2Shared::DestroyBuffers` | 602 | 1 |

## Where the add sits

**`stream_bytes`.** `IBufLock` takes the stream branch when `pool->m_usage == GxPoolUsage_Stream` (`CGxDeviceVulkan.cpp:1596`). The lock size is `buf->m_size`, which `CGxDevice::BufStream` stored as `itemCount * itemSize` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1071`). Add it at the `StreamLock` call (`CGxDeviceVulkan.cpp:1605`). That call runs only when `m_itemSize` and `m_size` are both non-zero; the return at line 1598 is the other stream exit. `VkContext::StreamLock` (`VkContext.cpp:1086`) forwards those bytes into `BumpChunk` with `StreamRingSize` (`VkContext.h:135`, `64 << 20`). `BufLock` (`CGxDeviceVulkan.cpp:1674`) and `BufData` (`CGxDeviceVulkan.cpp:1693`) both enter `IBufLock`, so this one add covers them. Static and dynamic locks leave through `IPoolBuffer` at line 1643 and stay out of `stream_bytes`.

**`rigid_stream_bytes`.** `DrawRigidDoodads` locks a vertex stream (`CM2SceneRender.cpp:644`–`645`, 32 bytes per vertex) and an index stream (`667`–`668`, 2 bytes per index). Add that buffer’s `m_size` when the lock pointer is non-null. A failed index lock still keeps the vertex bytes already added.

**`skin_locks`.** One per successful stream lock that skins: the fixed-function doodad batch (`DrawBatchDoodad` `777`, then `FillDoodadSkin` at `795` or `797`), `CM2Model::SetVertices` (`2702`, then `s_transformVertices`), and `CM2Model::SetVerticesMultiSample` (`4521`). `FillDoodadSkin` (`598`) runs once per worker range inside one lock. The lock is the count. `SetBatchVertices` (`CM2SceneRender.cpp:190` and `188`) is what reaches the two `CM2Model` locks while shaders are off.

**`uniform_uploads`.** `Draw` always builds a `VkDrawUniforms` at `1359` when it gets that far. The upload is the `memcpy` into the uniform ring at `1385`, taken when `!m_uniformsValid` or the 768-byte `memcmp` at `1377` differs. Each upload copies `sizeof(VkDrawUniforms)` bytes. That size is 768 (`CGxDeviceVulkan.h:46`). The ring is `UniformRingSize` (`VkContext.h:132`, `32 << 20`). A full ring returns at `1383` before the copy.

**`descriptor_binds`.** `vkCmdBindDescriptorSets` at `1442` runs for every draw that reaches it. The set cache ends at `1439`. The bind follows that cache.

**`draws`.** One per issued command: `vkCmdDrawIndexedIndirect` (`1467`), `vkCmdDrawIndexed` (`1470`), `vkCmdDraw` (`1474`). Returns before those lines are not draws. `DrawInstanced` (`1499`) and `DrawTerrainBatch` (`1538`) call `Draw`, so they are counted there. One indirect terrain command is one draw. Its chunk count is `terrain_batch_size` / `terrain_batches`, and `m_indirectCount` is the indirect command count inside that one draw.

**`terrain_batch_size`.** Once per non-empty flush, add `s_terrainBatch.size()` where `used` is constructed (`MapRenderChunk.cpp:2572`). The empty flush returns at `2560`. Each element is one `TerrainCapture` pushed by `QueueMultiPass` (`2556`). Chunks that take `RenderMultiPassAlpha` (`2538`) never enter `s_terrainBatch`. `FlushMultiPass` runs at the end of each layer loop in `RenderChunksSinglePass` (`World.cpp:3728`).

**`terrain_batches`.** Add 1 at each `DrawTerrainBatch` call (`MapRenderChunk.cpp:2625`). That call is one texture group. The fallback loop at `2630` draws the same group one chunk at a time when `DrawTerrainBatch` returns 0. It is still one batch.

**`vis_flush_allocs`.** `FlushQueuedVisibility` swaps `s_visQueue` into a local vector (`MapChunk.cpp:519`–`520`). The swap does not allocate. Then `std::vector<VisJob> jobs(requests.size())` (`524`) allocates. The queue was non-empty, so this size is at least 1. `std::vector<LiquidOut> liquids(liquidTotal)` (`549`) allocates when `liquidTotal > 0`.

**`doodad_cull_allocs`.** `s_doodadCulls.clear()` (`World.cpp:3925`) keeps capacity. Add 1 at `push_back` (`3957`) when `size() == capacity()` before the push. That is the third flush allocation, beside `jobs` and `liquids`.

**`terrain_flush_allocs`.** `std::vector<uint8_t> used(s_terrainBatch.size())` (`2572`) allocates once per non-empty flush. `std::vector<GxTerrainDraw> draws` (`2573`) allocates on `push_back` (`2603`) when `size() == capacity()`. `draws.clear()` (`2585`) keeps capacity for later groups in that same flush. The next flush constructs a new `draws`.

**`stream_lock_news`.** Add 1 at each `new VkBufferAlloc` inside the stream branch: the borrowed view (`1619`, only when the pool has no wrapper yet) and the owning wrapper (`1634`). Reusing an existing borrowed wrapper assigns through `*alloc = slice` (`1623`) and does not add.

**`vk_allocs`.** Add 1 when `vkAllocateMemory` returns `VK_SUCCESS`. Image blocks go through `VkHeap::Alloc` (`VkContext.cpp:51`); `BlockSize` is `64 << 20` (`VkContext.h:56`). Host-visible vertex, index, uniform, staging, and stream chunks go through `VkContext::CreateBuffer` (`VkContext.cpp:1003`). `AcquireBuffer` (`970`) and `BumpChunk` (`1024`, `1034`) call `CreateBuffer`, so they are already in this count.

**`grass_fill_vertices`.** Add 1 on entry to `CDetailDoodadInstBatch::FillVertices` (`DetailDoodad.cpp:340`). Callers are `FillMappedRange` (`617`) and `FillVerticesOnMain` (`632`). Both run only after a has-data check (`674`, `892`). `SWING::FillVertices` (`WoW/Source/Game/GameClient/WTOBJECT.cpp:68`) is a different function.

**`use_streaming_bufs`.** Add 1 on entry to `CMapRenderChunk::UseStreamingBufs` (`MapRenderChunk.cpp:889`). The only caller is `RenderSetup` (`934`), in the branch where `m_chunkBuf` is null (`930`–`934`).

**`destroy_buffers`.** Add 1 on entry to `CM2Shared::DestroyBuffers` (`M2Shared.cpp:602`). Callers: `AllocInstances` when the instance capacity steps up (`779`), `CM2Cache::UpdateShared` for a shared unused for 10 seconds (`M2Cache.cpp:413`), and `CM2Shared` teardown (`M2Shared.cpp:1882`). The steady-camera grow is `779`.

## Sites that stay off the line

`World::GetFramerate` (`World.cpp:4809`) averages 30 tick samples. It is not this log.

`CGxDeviceD3d::IBufLock` (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:141`) and `CGxDeviceOpenGl::BufLock` (`Engine/Source/Gx/CGxDeviceOpenGL/CGxDeviceOpenGl.cpp:1114`) lock streams on those devices. `stream_bytes` is the Vulkan ring lock. `GxDefaultApi` (`Engine/Source/Gx/Gx.cpp:128`) selects Vulkan when `WOWDECOMP_VULKAN` is on and the loader is present.

`CancelOwnedFill` (`MapRenderChunk.cpp:206`) and `DropQueuedVisibility` (`MapChunk.cpp:390`) erase in the middle of a vector. They do not allocate.

`CMapChunk::CreateIndices` (`MapChunk.cpp:302`) writes the 8×8 index pattern. The per-frame miss counters are `use_streaming_bufs` and `grass_fill_vertices`.

`CMapRenderChunk::UpdatePools` (`MapRenderChunk.cpp:831`) sets `s_pnEstimateIndex` to `768 * count` when the pools are dirty. That is a pool size, not a per-frame add.

The stream-ring wrap is `StreamRingSize` (`VkContext.h:135`). `BumpChunk` (`VkContext.cpp:1018`) does not allocate a fresh pool on wrap. No wrap counter.
