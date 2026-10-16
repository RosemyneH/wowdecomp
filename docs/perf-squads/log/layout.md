# Campaign 0 — Frame log layout

Default-off counters for the sites Campaigns 1–6 name. One line per second through the existing console. Draw, cull, lock, and return paths stay as they are. `World::GetFramerate` stays the 30-sample tick average at `WoW/Source/WorldClient/World.cpp:4809`.

The first slice prints the full line below, zeros included. Later slices only add `PerfAdd` / `PerfZone` at the sites. Key names and order stay fixed.

## New files

| File | Role |
| --- | --- |
| `Engine/Source/Perf/PerfLog.h` | `PerfSlots`, the idle gate, `PerfZone` |
| `Engine/Source/Perf/PerfLog.cpp` | The one `g_perf`, init, the one-second line, the `perflog` command |
| `Engine/Source/Perf/CMakeLists.txt` | Static lib `Perf`, same shape as `WoW/Source/ObjectAlloc/CMakeLists.txt` |

`CMakeLists.txt` gains `add_subdirectory(Engine/Source/Perf)` beside the other Engine libs (`CMakeLists.txt:137`). `WoW/Source/CMakeLists.txt` gains `Perf` inside `WOW_LIBS` so the Linux rescan group resolves `Gx` and `WorldClient` to it.

`PerfLog.h` includes `<atomic>`, `<cstdint>`, and `Engine/Source/Os/TimeManager.h`. It stays free of Vulkan and World headers. New comments in these files use the repo TextEmoji form.

## Enable switch

`PerfLogInit` runs once from `InitializeEngineCallback`, immediately after `HeapUsageInitialize()` (`WoW/Source/Client.cpp:1080`). `PerfLogDestroy` unregisters the command beside `HeapUsageDestroy()` (`WoW/Source/Client.cpp:1652`, `WoW/Source/ObjectAlloc/HeapUsage.cpp:9`).

```cpp
const char* env = getenv("WOW_PERFLOG");
g_perfOn.store(env && env[0] == '1' && env[1] == '\0' ? 1 : 0, std::memory_order_relaxed);
```

Same `getenv` style as `WOWDECOMP_VK_VALIDATION` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:117`). The value `1` arms the log for the process. Any other value, including an unset variable, leaves `g_perfOn` at 0.

`Job::Init` runs later, so this store happens before worker threads exist. The hot gate is one relaxed load:

```cpp
inline void PerfAdd(std::atomic<uint64_t>& slot, uint64_t n) {
    if (!g_perfOn.load(std::memory_order_relaxed)) {
        return;
    }
    slot.fetch_add(n, std::memory_order_relaxed);
}
```

While `g_perfOn` is 0 the load returns. `OsGetAsyncTimeClocks` stays behind that load. `fetch_add` stays behind it. The frame loop pays one relaxed load inside `PerfLogTick` and then returns.

The console command `perflog` does not arm the counters. The environment variable is the switch.

## Where the atomics live

One file-scope `PerfSlots g_perf` in `PerfLog.cpp`, zero-initialized, process lifetime, fixed size. No heap, no vector, no per-frame buffer in the log itself. `extern PerfSlots g_perf` and `extern std::atomic<uint8_t> g_perfOn` are the header surface.

Every printed counter is a `std::atomic<uint64_t>`. Main-thread sites and worker sites use the same `fetch_add` (`std::memory_order_relaxed`). `ParallelFor` joins before `CWorld::Render` returns, and `PerfLogTick` runs after that return, so the exchange sees this frame’s adds.

Worker-written slots sit on their own cache line (`alignas(64)`): `grass_fill`, `grass_bytes`, `idx_write`, `idx_bytes`. Those four are incremented from `FillVertices` and `CreateIndices`, which run inside `Job::ParallelFor`. The other slots are main-thread only and stay packed.

Window state next to `g_perf`, main thread only, not printed as its own slot: `g_windowStart` (clocks), `g_scene[16]` (NUL-terminated token, default `none`), `g_last[2048]` (last formatted line).

## Zones

`PerfZone` is an inline scope guard in the header. The constructor loads `g_perfOn`. When that is 0, `start` stays 0 and the destructor returns. When it is 1, the constructor stores `OsGetAsyncTimeClocks()` and the destructor `fetch_add`s the delta into a `*_clocks` slot.

```cpp
inline PerfZone::PerfZone(std::atomic<uint64_t>& s) : slot(&s), start(0) {
    if (g_perfOn.load(std::memory_order_relaxed)) {
        start = OsGetAsyncTimeClocks();
    }
}
```

Timers are `OsGetAsyncTimeClocks` (`Engine/Source/Os/TimeManager.h:30`). On Linux, `SystemMethod2` reads `CLOCK_MONOTONIC` in nanoseconds (`Engine/Source/Os/Linux/TimeManager.cpp:59-61`). `Calibrate` sets `m_performanceFrequency` to `1000000000` (`Engine/Source/Os/Linux/TimeManager.cpp:24`). `OsGetAsyncClocksPerSecond` returns that frequency (`Engine/Source/Os/Linux/TimeManager.cpp:78-84`).

Slots store clocks. The dump converts once, on the main thread:

```text
us = clocks * 1000000 / OsGetAsyncClocksPerSecond()
```

The elapsed-window pattern is the taxi benchmark (`WoW/Source/Client.cpp:2091-2092`): subtract two `OsGetAsyncTimeClocks` samples and divide by `OsGetAsyncClocksPerSecond`. The log keeps the quotient in integer microseconds.

`z_frame_us` is the wall time of `CWorldScene::Render` and already contains the other zones. The other zones are subsets. Summing them double-counts the frame.

| Log key | Slot | Scope |
| --- | --- | --- |
| `z_frame_us` | `z_frame_clocks` | `CWorldScene::Render` body (`WoW/Source/WorldClient/CWorldSceneRender.cpp:71`) |
| `z_rigid_us` | `z_rigid_clocks` | `DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:608`) |
| `z_skin_us` | `z_skin_clocks` | CPU skin loop, the `else` at `CM2SceneRender.cpp:770-820` |
| `z_uni_us` | `z_uni_clocks` | `IBuildUniforms` through the upload store (`CGxDeviceVulkan.cpp:1359-1390`) |
| `z_terrain_us` | `z_terrain_clocks` | `QueueMultiPass` (`MapRenderChunk.cpp:2526`) and `FlushMultiPass` (`MapRenderChunk.cpp:2559`) |
| `z_grass_us` | `z_grass_clocks` | `PrepareGrassFills` (`DetailDoodad.cpp:719`) and `FillVerticesOnMain` (`DetailDoodad.cpp:626`) |
| `z_stream_us` | `z_stream_clocks` | `UseStreamingBufs` (`MapRenderChunk.cpp:889`) |
| `z_vis_us` | `z_vis_clocks` | `CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`) |
| `z_cull_us` | `z_cull_clocks` | `CWorldScene::CullDoodadsExterior` (`World.cpp:3924`) |

Zones stay on the main thread. A zone around `PrepareGrassFills` includes the `ParallelFor` wait. `FillVertices` itself is a counter, so worker CPU time is not added again on top of that wait.

## Who increments

`PerfAdd` only. Counts and byte totals use `sizeof` at the site. Failed Vulkan allocations stay at the old total.

### Stream total

`CGxDevice::BufStream` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1062`). After `m_size` is assigned (`CGxDevice.cpp:1071`), when `itemSize != 0 && itemCount != 0`:

- `stream_locks += 1`
- `stream_bytes += itemCount * itemSize`

The `BufStream(target, 0, 0)` identity probe (`Engine/Source/Model2/M2Shared.cpp:606`) stays out of both. `stream_*` is the device-wide total. `rigid_*` and `skin_*` are the M2 rows already inside it.

### Campaign 1 — M2 mesh

| Slot | Site |
| --- | --- |
| `rigid_locks`, `rigid_bytes += vertexCount * 32` | Vertex lock held (`CM2SceneRender.cpp:644-645`) |
| `rigid_locks`, `rigid_bytes += indexCount * 2` | Index lock held (`CM2SceneRender.cpp:667-668`) |
| `skin_locks`, `skin_bytes += instances * vertexCount * 32` | Skin lock held (`CM2SceneRender.cpp:776-777`) |
| `inst_grow` | `AllocInstances` calls `DestroyBuffers` (`M2Shared.cpp:779`) |
| `buf_destroy` | Entry of `CM2Shared::DestroyBuffers` (`M2Shared.cpp:602`) |

A grow increments `inst_grow` and `buf_destroy`. Shader-on skips `DrawRigidDoodads` (`CM2SceneRender.cpp:717`), so `rigid_*` and `z_rigid_us` stay 0 on that path. The skin `else` is the `z_skin_us` scope.

### Campaign 2 — uniforms and draws

All inside `CGxDeviceVulkan::Draw` (`CGxDeviceVulkan.cpp:1183`).

| Slot | Site |
| --- | --- |
| `draws` | After the early return (`CGxDeviceVulkan.cpp:1194-1196`) |
| `uni_pack` | Entry of `IBuildUniforms` (`CGxDeviceVulkan.cpp:971`), the only caller being `CGxDeviceVulkan.cpp:1359` |
| `uni_upload`, `uni_bytes += sizeof(VkDrawUniforms)` | Ring `memcpy` (`CGxDeviceVulkan.cpp:1385`). `sizeof` is 768 (`CGxDeviceVulkan.h:46`) |
| `desc_bind` | `vkCmdBindDescriptorSets` (`CGxDeviceVulkan.cpp:1442`), including a bind whose set and offset were already current |

`DrawInstanced` (`CGxDeviceVulkan.cpp:1499`) and `DrawTerrainBatch` (`CGxDeviceVulkan.cpp:1538`) reach `Draw`, so each counts as one `draws`. D3D leaves `draws`, `uni_*`, and `desc_bind` at 0. `stream_*` still moves, because `BufStream` is the base device.

### Campaign 3 — terrain batch

| Slot | Site |
| --- | --- |
| `light_cap` | `CaptureLights(cap)` (`MapRenderChunk.cpp:2548`) |
| `terrain_cap`, `terrain_bytes += sizeof(TerrainCapture)` | `s_terrainBatch.push_back` (`MapRenderChunk.cpp:2556`) |
| `terrain_flush`, `terrain_batch += size`, `terrain_batch_max` high-water, `flush_alloc += 2` | `FlushMultiPass` after the empty return (`MapRenderChunk.cpp:2560-2573`) |
| `pair_scan` | One per inner `j` iteration (`MapRenderChunk.cpp:2587`) |
| `terrain_draw_bytes += sizeof(GxTerrainDraw)` | `draws.push_back` (`MapRenderChunk.cpp:2603`). Record type is `CGxDevice.h:72` |

`terrain_batch` is the sum of `s_terrainBatch.size()` across flushes in the window. `terrain_batch_max` is the largest single flush.

### Campaign 4 — remesh

| Slot | Site | Thread |
| --- | --- | --- |
| `grass_miss` | Null vertex and index in `AcquireBuffers` (`DetailDoodad.cpp:130-131`) | Main |
| `grass_fill`, `grass_bytes += m_vertexCount * sizeof(CGxVertexPNCT)` | Entry of `FillVertices` (`DetailDoodad.cpp:340`) | Worker, from `FillMappedRange` (`DetailDoodad.cpp:617`), and main from `FillVerticesOnMain` |
| `stream_refill` | Entry of `UseStreamingBufs` (`MapRenderChunk.cpp:889`), reached from `RenderSetup` when `m_chunkBuf` is null (`MapRenderChunk.cpp:933-934`) | Main |
| `alloc_miss` | Both `return nullptr` paths in `AllocBuf` (`MapRenderChunk.cpp:601`, `MapRenderChunk.cpp:636`) | Main |
| `idx_write`, `idx_bytes += totalIndices * sizeof(uint16_t)` | Return of `CreateIndices(char*)` (`MapChunk.cpp:302`, return at `MapChunk.cpp:337`) | Worker, via `FillLockedBufs` (`MapRenderChunk.cpp:189`) |

`grass_miss` is the pool-short path. `Draw` returns when the buffers stayed null (`DetailDoodad.cpp:888-889`).

### Campaign 5 — visibility scratch

| Slot | Site |
| --- | --- |
| `vis_flush` | `FlushQueuedVisibility` after the empty return (`MapChunk.cpp:515-517`). `UpdatePools` calls it (`MapRenderChunk.cpp:806`) |
| `vis_jobs += jobs.size()`, `vis_bytes += jobs.size() * sizeof(VisJob)` | `jobs` construction (`MapChunk.cpp:524`) |
| `vis_bytes += liquids.size() * sizeof(LiquidOut)` | `liquids` construction (`MapChunk.cpp:549`) |
| `vis_erase` | One per erased element in `DropQueuedVisibility` (`MapChunk.cpp:393`) and `CancelOwnedFill` (`MapRenderChunk.cpp:209`) |
| `doodad_cull` | Entry of `CullDoodadsExterior` (`World.cpp:3924`) |
| `doodad_n += s_doodadCulls.size()` | After the push loop (`World.cpp:3960`) |

### Campaign 6 — Vulkan allocations

Successful `vkAllocateMemory` only.

| Slot | Site |
| --- | --- |
| `vk_alloc`, `vk_heap`, `vk_bytes += info.allocationSize` | `VkHeap::Alloc` after a successful allocate (`VkContext.cpp:51`) |
| `vk_alloc`, `vk_buf`, `vk_bytes += allocInfo.allocationSize` | `VkContext::CreateBuffer` after a successful allocate (`VkContext.cpp:1003`) |
| `buf_new` | `new VkBufferAlloc` in `IPoolBuffer` (`CGxDeviceVulkan.cpp:1544`) and both `IBufLock` stream arms (`CGxDeviceVulkan.cpp:1619`, `CGxDeviceVulkan.cpp:1634`) |

`vk_alloc` is `vk_heap + vk_buf`. `buf_new` is the heap `VkBufferAlloc` objects, separate from `vkAllocateMemory`.

Nameplates, portal cover, and `TSFixedArray::ReallocData` stay off this line. They are watch-list items.

## One-second line

`PerfLogTick` runs from `CWorld::Render` after `CWorldScene::Render` returns and before `CheckAndSendBotDetected` (`WoW/Source/WorldClient/World.cpp:765-767`).

```cpp
void CWorld::Render(const C3Vector& cameraPos, float time) {
    {
        PerfZone frame(g_perf.z_frame_clocks);
        CWorldScene::Render(cameraPos, time);
    }
    PerfLogTick();
    CheckAndSendBotDetected();
}
```

`PerfLogTick` while disarmed returns on the `g_perfOn` load. While armed:

1. First call stores `g_windowStart = OsGetAsyncTimeClocks()` and returns. That partial window is not printed.
2. Each later call adds 1 to `frames`.
3. When `now - g_windowStart` reaches `OsGetAsyncClocksPerSecond()`, `exchange(0)` every slot, format, `ConsoleWrite`, then set `g_windowStart = now`.

`frames` includes the frame that closes the window. `dt_us` is that window, so it sits at or just over one second.

Format into `char line[2048]` with `SStrPrintf`, then one `ConsoleWrite(line, DEFAULT_COLOR)`. `ConsoleWrite` is `WoW/Source/Console/ConsoleClient.cpp:873`. A scene run passes the existing `consolelog` flag (`WoW/Source/Client.cpp:150`), which enables the tee at `Client.cpp:1683`. The tee writes one stdout line and appends a newline when the buffer has none (`ConsoleClient.cpp:886`).

Frozen line, keys in this order, one space between tokens, no spaces inside a token:

```text
perf scene=%s dt_us=%llu frames=%llu z_frame_us=%llu z_rigid_us=%llu z_skin_us=%llu z_uni_us=%llu z_terrain_us=%llu z_grass_us=%llu z_stream_us=%llu z_vis_us=%llu z_cull_us=%llu stream_locks=%llu stream_bytes=%llu vk_alloc=%llu vk_bytes=%llu uni_upload=%llu uni_bytes=%llu desc_bind=%llu terrain_batch=%llu terrain_batch_max=%llu draws=%llu vis_flush=%llu rigid_locks=%llu rigid_bytes=%llu skin_locks=%llu skin_bytes=%llu inst_grow=%llu buf_destroy=%llu uni_pack=%llu terrain_cap=%llu terrain_bytes=%llu light_cap=%llu terrain_flush=%llu flush_alloc=%llu pair_scan=%llu terrain_draw_bytes=%llu grass_miss=%llu grass_fill=%llu grass_bytes=%llu stream_refill=%llu alloc_miss=%llu idx_write=%llu idx_bytes=%llu vis_jobs=%llu vis_bytes=%llu doodad_cull=%llu doodad_n=%llu vis_erase=%llu vk_heap=%llu vk_buf=%llu buf_new=%llu
```

`scene` is the only non-integer. Integers are `unsigned long long` decimals. A later campaign appends new keys at the end in its own layout. This campaign’s writer leaves the middle of the line alone.

Outdoor close needs non-zero `stream_locks`, `uni_upload`, `terrain_batch`, and `vis_flush` on a one-second line. Those are samples from the sites above, not forced totals.

## Console command

Register beside the HeapUsage commands (`WoW/Source/ObjectAlloc/HeapUsage_Console.cpp:12`, register at `HeapUsage_Console.cpp:68-71`):

```cpp
ConsoleCommandRegister("perflog", PerfLogCommand, CATEGORY_DEBUG, "perflog [outdoor|city|interior|crowd]");
```

`COMMANDHANDLER` is `int32_t (*)(const char* command, const char* arguments)` (`Engine/Source/ConsoleBase/ConsoleCommand.h:11`). `ConsoleCommandRegister` is `ConsoleCommand.h:36`. `CATEGORY_DEBUG` is `Engine/Source/ConsoleBase/Types.h:8`. Return 1, matching `HeapUsage`.

| Arguments | Result |
| --- | --- |
| `outdoor`, `city`, `interior`, `crowd` | Copy the token into `g_scene`. Later lines carry it. |
| anything else | `ConsoleWrite` the current tag and leave `g_scene` as it was |
| empty | `ConsoleWrite` `g_last` |

Empty and disarmed writes `perf off`. Empty, armed, and still inside the first window writes `perf pending`. The command never stores `g_perfOn`.
