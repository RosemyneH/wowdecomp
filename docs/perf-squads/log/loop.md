# Campaign 0 — Loop

**Status: NEXT SLICE.** Slice 1 of 6, `Declare the counters`. Capture count is **0 / 4** scene lines. CLOSED is unmet.

No `FrameLog` symbol and no `WOWDECOMP_FRAME_LOG` define exist in the tree. `World::GetFramerate` (`WoW/Source/WorldClient/World.cpp:4809`) is a 30-sample tick average. `HeapUsage` (`WoW/Source/ObjectAlloc/HeapUsage_Console.cpp:12`) counts `ObjectAlloc` only. `Script_TogglePerformanceDisplay`, `Script_TogglePerformancePause`, and `Script_TogglePerformanceValues` (`WoW/Source/Ui/GameUI_Scripts.cpp:6150`, `:6156`, `:6162`) return 0. None of those is the campaign line.

No-gain close does not apply: zero writer cycles have landed, so there is no pair of cycles to compare. Faith has not rejected an approach.

## CLOSED

CLOSED when one capture log holds **4** lines, one for each scene token below, and **every** line meets **every** numeric gate in the same row. Fewer than 4 lines, a missing field, a renamed field, or any gate miss stays NEXT SLICE.

Scenes, same character position and camera on every rerun of that scene:

| Token | What the camera must contain |
| --- | --- |
| `outdoor` | Terrain, doodads, and grass at Elwynn / Westfall density |
| `city` | Many skinned and rigid M2s |
| `interior` | Portal-heavy WMO (Ironforge) |
| `crowd` | Many nameplates |

| Gate | Value |
| --- | --- |
| Lines | `>= 1` per token, **4** tokens, each one `ConsolePrintf` line (`WoW/Source/Console/ConsoleClient.cpp:941`) |
| `dt_s` | `>= 1.0` and `< 2.0` |
| `stream_locks` | `>= 1` |
| `uniform_uploads` | `>= 1` |
| `terrain_batch` | `>= 1` |
| `vis_flushes` | `>= 1` |
| `stream_bytes` | `>= stream_locks` |
| `allocs` | `>= vis_flushes` |
| `draws`, `vk_alloc`, `descriptor_binds`, `m2_pool_rebuilds` | Present, `>= 0`. Zero is a valid sample |
| Six `zone_*_ns` fields | Present, `>= 0`. Zero is a valid sample |

`dt_s` is `OsGetAsyncTimeClocks` delta divided by `OsGetAsyncClocksPerSecond`, the conversion already used at `WoW/Source/Client.cpp:2091`. On Linux, `SystemMethod2` is `CLOCK_MONOTONIC` nanoseconds (`Engine/Source/Os/Linux/TimeManager.cpp:59`) and the per-second divisor is `1_000_000_000` (`Engine/Source/Os/Linux/TimeManager.cpp:24`, `Engine/Source/Os/Linux/TimeManager.cpp:78`). A line with `dt_s >= 2` is a hitch, not a one-second sample, and does not count. A line with `dt_s < 1` does not count.

`ConsolePrintf` drops the line when no console device exists and the log tee is off (`WoW/Source/Console/ConsoleClient.cpp:942`). A dropped line does not count. The confirm slice turns the tee on (`Engine/Source/ConsoleBase/IConsole.h:12`) or runs with the console device up.

`stream_bytes >= stream_locks` because a counted lock is the success return at `CGxDeviceVulkan::IBufLock` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1640`), which sits after the zero-size reject (`CGxDeviceVulkan.cpp:1598`). `allocs >= vis_flushes` because every non-empty `CMapChunk::FlushQueuedVisibility` (`WoW/Source/WorldClient/MapChunk.cpp:514`) constructs `jobs` with a non-zero size (`MapChunk.cpp:524`) after the empty return (`MapChunk.cpp:515`).

`vk_alloc == 0` on a steady second is still a passing line. Campaign 6 reads that flatness later.

Zone totals may exceed `dt_s`. `zone_draw_ns` wraps `CGxDeviceVulkan::Draw`, and the M2 zones call into Draw. Print the raw sums. Do not subtract them.

## Line format

One line, this field order, this spelling, trailing newline. `ConsolePrintf` does not add a newline (`WoW/Source/Console/ConsoleClient.cpp:949`). The buffer cap is 4096 (`ConsoleClient.cpp:946`). Later squads do not rename or reorder fields. The freeze slice confirms this string after a scene run; it does not invent a second spelling.

```
frame scene=%s dt_s=%.3f draws=%llu stream_locks=%llu stream_bytes=%llu allocs=%llu vk_alloc=%llu uniform_uploads=%llu descriptor_binds=%llu terrain_batch=%llu vis_flushes=%llu m2_pool_rebuilds=%llu zone_m2_rigid_ns=%llu zone_m2_skin_ns=%llu zone_terrain_ns=%llu zone_grass_ns=%llu zone_vis_ns=%llu zone_cull_ns=%llu zone_draw_ns=%llu\n
```

`scene` is one of `outdoor`, `city`, `interior`, `crowd`.

`terrain_batch` is the **max** of `s_terrainBatch.size()` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2461`) sampled once per `FlushMultiPass` that gets past the empty return (`MapRenderChunk.cpp:2560`). It is not a sum of flushes.

`uniform_uploads` counts the ring `memcpy` (`CGxDeviceVulkan.cpp:1385`) inside the `memcmp` miss (`CGxDeviceVulkan.cpp:1377`). It does not count `IBuildUniforms` (`CGxDeviceVulkan.cpp:971`, called from `CGxDeviceVulkan.cpp:1359`) when the 768-byte record matches. `sizeof(VkDrawUniforms) == 768` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:46`).

`descriptor_binds` counts `vkCmdBindDescriptorSets` at `CGxDeviceVulkan.cpp:1442`, including calls whose set and offset did not change. That call sits outside the set-rebuild test at `CGxDeviceVulkan.cpp:1399`.

`draws` counts one GPU command: `vkCmdDrawIndexedIndirect` (`CGxDeviceVulkan.cpp:1467`), `vkCmdDrawIndexed` (`CGxDeviceVulkan.cpp:1470`), or `vkCmdDraw` (`CGxDeviceVulkan.cpp:1474`). Not per instance, and not `m_indirectCount`.

`stream_locks` / `stream_bytes` increment once, at the stream success return `CGxDeviceVulkan.cpp:1640`, by `1` and `buf->m_size`. Callers such as `DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:645`, `:668`), `CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp:2702`), and `FillVerticesOnMain` (`WoW/Source/WorldClient/DetailDoodad.cpp:629`) already enter `IBufLock`. Do not increment at those callers.

`vk_alloc` increments once per `vkAllocateMemory` that returns `VK_SUCCESS`: `VkHeap::Alloc` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:51`) and `VkContext::CreateBuffer` (`VkContext.cpp:1003`). A failed call returns before the success path (`VkContext.cpp:51`, `:1003`) and stays at 0.

`vis_flushes` increments once per `FlushQueuedVisibility` that swaps a non-empty queue (`MapChunk.cpp:520`), after the empty return (`MapChunk.cpp:515`).

`allocs` increments by 1 at each of these hits, and nowhere else:

- `std::vector<VisJob> jobs(requests.size())` at `MapChunk.cpp:524`
- `std::vector<LiquidOut> liquids(liquidTotal)` at `MapChunk.cpp:549` when `liquidTotal > 0`
- `std::vector<uint8_t> used(s_terrainBatch.size())` at `MapRenderChunk.cpp:2572`
- `new VkBufferAlloc` at `CGxDeviceVulkan.cpp:1619` and `CGxDeviceVulkan.cpp:1634`

The default-constructed `std::vector<GxTerrainDraw> draws` at `MapRenderChunk.cpp:2573` does not allocate. Do not count it. `CM2Shared::DestroyBuffers` is `m2_pool_rebuilds`, not `allocs`.

`m2_pool_rebuilds` increments at the `DestroyBuffers` call inside `CM2Shared::AllocInstances` (`Engine/Source/Model2/M2Shared.cpp:779`). The definition is `M2Shared.cpp:602`. The teardown call at `M2Shared.cpp:1882` is not this counter.

Zone accumulators are nanoseconds from `OsGetAsyncTimeClocks` (`Engine/Source/Os/TimeManager.h:30`). Brackets:

| Field | Bracket |
| --- | --- |
| `zone_m2_rigid_ns` | `DrawRigidDoodads` (`CM2SceneRender.cpp:608`) |
| `zone_m2_skin_ns` | `FillDoodadSkin` (`CM2SceneRender.cpp:598`) and `CM2Model::SetVertices` (`M2Model.cpp:2700`) |
| `zone_terrain_ns` | `CMapRenderChunk::QueueMultiPass` (`MapRenderChunk.cpp:2526`) and `CMapRenderChunk::FlushMultiPass` (`MapRenderChunk.cpp:2559`) |
| `zone_grass_ns` | `CDetailDoodadInstBatch::Draw` (`DetailDoodad.cpp:873`), which calls `AcquireBuffers` (`DetailDoodad.cpp:117`) and `FillVerticesOnMain` (`DetailDoodad.cpp:626`) |
| `zone_vis_ns` | `CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`) |
| `zone_cull_ns` | `CWorldScene::CullDoodadsExterior` (`WoW/Source/WorldClient/World.cpp:3924`) |
| `zone_draw_ns` | `CGxDeviceVulkan::Draw` (`CGxDeviceVulkan.cpp:1183`) after the early return at `CGxDeviceVulkan.cpp:1194` |

## NEXT SLICE — writer task

Land **only** the declaration. One concern. No call-site edits, no `ConsolePrintf`, no scene run.

1. In `CMakeLists.txt`, immediately after the signature-check `endif ()` at `CMakeLists.txt:113` and before the third-party comment at `CMakeLists.txt:115`, add an option defaulting **OFF**, same shape as `WOWDECOMP_DISABLE_SIGNATURE_CHECKS` (`CMakeLists.txt:110`):

```cmake
option(WOWDECOMP_FRAME_LOG "Compile-out frame counters, dumped through the console" OFF)
if (WOWDECOMP_FRAME_LOG)
    add_compile_definitions(WOWDECOMP_FRAME_LOG=1)
endif ()
```

Do not define the macro when the option is OFF.

2. Add header-only `Engine/Source/Base/FrameLog.h`. Include guard `ENGINE_SOURCE_BASE_FRAMELOG_H`, matching `Engine/Source/Base/Base.h:1`. Do not include it from `Base.h`. Do not add a `.cpp`. Do not edit `Engine/Source/Base/CMakeLists.txt` (that GLOB is `*.cpp` only, `Base/CMakeLists.txt:1`). Gx and World already include from the repo root (`Engine/Source/Gx/CMakeLists.txt:18`, `Engine/Source/Base/CMakeLists.txt:6`).

3. The project is C++17 (`CMakeLists.txt:23`). Counters are **namespace-scope `inline std::atomic<uint64_t>`**, `memory_order_relaxed`. Not function-local statics (those would split per TU). When `WOWDECOMP_FRAME_LOG` is unset, every macro expands to `((void)0)`, the atomics are not declared, and `OsGetAsyncTimeClocks` is not called.

4. Declare these names, matching the line format above: `draws`, `stream_locks`, `stream_bytes`, `allocs`, `vk_alloc`, `uniform_uploads`, `descriptor_binds`, `terrain_batch`, `vis_flushes`, `m2_pool_rebuilds`, `zone_m2_rigid_ns`, `zone_m2_skin_ns`, `zone_terrain_ns`, `zone_grass_ns`, `zone_vis_ns`, `zone_cull_ns`, `zone_draw_ns`. Plus `enum class Scene` with `Outdoor`, `City`, `Interior`, `Crowd`, a `SetScene` that is empty when the flag is off, an add helper, a max helper for `terrain_batch`, and a zone scope whose destructor adds the clock delta. Macros `FRAME_LOG_ADD`, `FRAME_LOG_MAX`, `FRAME_LOG_ZONE` take the counter name and compile out without looking that name up when the flag is off.

5. A new comment uses the repo TextEmoji form. No comment is fine. Do not touch `// OFFSET:` functions. Do not change `VkDrawUniforms` (`CGxDeviceVulkan.h:22`).

Stop there. Wiring, printing, the scene run, and the format freeze are later slices.

## Slices after this one

| # | Slice | Opens when |
| --- | --- | --- |
| 2 | Wire outdoor, M2, terrain, and grass sites | This header exists, the option defaults OFF, and no call site includes it |
| 3 | Wire Vulkan alloc, uniform, and descriptor sites | Slice 2's sites increment the atomics named above |
| 4 | Print the line through `ConsolePrintf` | Slice 3's sites increment `vk_alloc`, `uniform_uploads`, `descriptor_binds`, `draws` |
| 5 | Confirm one steady run of each scene | A flagged build emits the format string once per second |
| 6 | Freeze the format | Four lines exist and each meets the CLOSED gates |

Loop writes CLOSED only when the gates hold. A declaration without a scene line stays NEXT SLICE.
