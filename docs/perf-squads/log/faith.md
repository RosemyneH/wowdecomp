# Faith — Campaign 0 frame log

**Verdict: PASS** for a compile-out, default-off counter. The current tree has no frame-log flag, no counter atomics, and no log dump on the draw, cull, seed, or uniform paths. The default binary therefore cannot take a counter path, and it cannot move draw results, cull decisions, placement seeds, or the 768-byte `VkDrawUniforms` / 448-byte `GpuDrawItem` records.

This review is of that tree plus the hook sites Campaigns 1–6 name. An enabled counter is a different binary. At the job sites below it changes which worker wins the next chunk. The stored results stay on indexed slots and are applied after the barrier, so the enabled path still leaves draws, culls, and seeds alone when the hook is only a clock read and an atomic increment.

`OsGetAsyncTimeClocks` itself only calls `clock_gettime` (`Engine/Source/Os/Linux/TimeManager.cpp:59`). The existing call in `ClientUpdateTaxiBenchmarkMode` (`WoW/Source/Client.cpp:2091`) is the taxi benchmark, behind `s_clientTaxiBenchEnabled`, and is not a frame log.

## Invariants

| Result | Invariant | Where |
| --- | --- | --- |
| PASS | No counter, zone timer, or log line on the render, cull, or seed paths. | Tree-wide. `OsGetAsyncTimeClocks` sites are the taxi benchmark, Lua profile, and `FrameScript` only. |
| PASS | `VkDrawUniforms` is 768 bytes. Field order matches `FfpUniforms.glsl` (5 `mat4`, then the vec4/ivec4 block through `misc`). | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:22` struct, `:46` `static_assert`. `Engine/Source/Gx/CGxDeviceVulkan/Shaders/FfpUniforms.glsl:1`. `Tools/spirv-pin-compare.py:75` rejects a block that is not 768. |
| PASS | Pack, compare, and upload use `sizeof(VkDrawUniforms)`. A missed dirty bit is still a full 768-byte `memcmp`. Overflow refuses the draw. | `CGxDeviceVulkan.cpp:971` `memset`, `:1377` `memcmp`, `:1381` overflow return, `:1385` `memcpy`. |
| PASS | `GpuDrawItem` is 448 bytes. `tex0` at 64, `tex1` at 128, `lightPos` at 192, `lightAtten` at 384. Shader record matches. | `VkContext.h:89`. `CGxDeviceVulkan.cpp:20`–`:24`. `Shaders/Ffp.vert:18`. |
| PASS | `GxTerrainDraw` keeps that same matrix and light prefix, then draw args. The GPU copy is field-wise into `GpuDrawItem`. | `CGxDevice.h:72`. `MapRenderChunk.cpp:2500` `FillTerrainDraw`. `CGxDeviceVulkan.cpp:1516` copies `world` through `lightAtten`, then writes `indexCount` / `firstIndex` / `baseVertex` into the indirect command. |
| PASS | Grass placement seed is the chunk offset, consumed only while the instance is built. | `MapChunk.cpp:1365`–`:1367` `SetSeed(m_cOffset.x \| (m_cOffset.y << 16))`. Draws `CRandom::uint32_` at `:1387`, `sfloat_` at `:1499`, `:1500`, `:1588`, `:1589`. `CreateDetailDoodads` runs from `RenderPrep` (`:1613`) on the main thread. |
| PASS | `FillVertices` reads `m_adds` already stored on the batch. It does not reseed. | `DetailDoodad.cpp:340`. Job body `:617`. |
| PASS | Hole test is `m_header->m_holes & CMap::s_holeMask`. Each live cell writes 12 indices. Dual-chunk vertices start at base 145. | `MapChunk.cpp:302`–`:337`. `MapRenderChunk.cpp:176`. |
| PASS | Index-pool estimate stays `768 * count`, with the far-clip radius capped at 160. | `MapRenderChunk.cpp:821`–`:831`. |
| PASS | Grass distance cap stays 220 yards. CVar default stays `70.0`. Fade uses `detailDoodadDist` and `dist * 0.85`. | `World.cpp:1316`, `:136`. `WorldParam_CVar.cpp:712`. `DetailDoodad.cpp:317`–`:321`. `MapChunk.cpp:1611`. |
| PASS | Exterior cull predicate is unchanged: fade level, frustum, occlusion `QueryBuffer >= 2`, unloaded-model barrier of `10.0f`, animate-inside-`100` yards. | `World.cpp:3930`, `:3912`, `:3916`, `:3942`, `:3986`. Sphere test is a read of the six planes (`CFrustum.cpp:68`). Occlusion query is a read of `s_clipBuffer` (`CWorldOcclusion.cpp:485`–`:524`). |
| PASS | Chunk visibility tests a snapshot frustum, then links buckets in job-array order after the barrier. | `MapChunk.cpp:404` snapshot, `:452` `RunChunkVisibility`, `:578` `ParallelFor`, `:580` `LinkChunkBucket` / `LinkLiquidBucket`. |
| PASS | Alpha batches stay layer 0, then later layers. Shadow draws run after the batches. Special chunks and foreign pools skip the batch. | `MapRenderChunk.cpp:2526` `QueueMultiPass` (`special` at `:2529`, pool test at `:2537`). `FlushMultiPass` layer loop `:2576`, blend `:2619`, shadow loop `:2637`. |
| PASS | Rigid doodads still stream the section mesh and call `DrawInstanced` with `m_boneMatrices[bone]`. One-bone rejection is the same. | `CM2SceneRender.cpp:608` `DrawRigidDoodads`, `:641` matrix gather, `:691` `DrawInstanced`. Caller `:717`. |
| PASS | `AllocInstances` still rounds up by 16 and calls `DestroyBuffers` on the way up. | `M2Shared.cpp:771`–`:779`. |
| PASS | Stream ring stays 64 MiB. `IBufLock` still heap-allocates a `VkBufferAlloc` around a borrowed view. `vkAllocateMemory` sites are unchanged. | `VkContext.h:135`. `CGxDeviceVulkan.cpp:1589` `IBufLock`, `:1619` and `:1634` `new VkBufferAlloc`. `VkContext.cpp:51` image heap, `:1003` buffer alloc. |
| PASS | `rand()` that feeds sequence variation is outside the parallel animate body. | `M2Model.cpp:1580` `SetupBoneSequence`, `:1687` `PickRandomSequenceVariation`. `AnimateMT` (`:5052`) updates times and calls `AnimateParticlesMT` / `AnimateAttachmentsMT`. It does not call `SetPrimaryBoneSequence`. |

## Job ordering when the counter is enabled

`Job::ParallelFor` (`Engine/Source/Base/Job/JobSystem.cpp:132`) publishes one epoch, then the caller and the workers take chunks with `g_pool.next.fetch_add` (`:47`). The chunk index fixes the item range (`:52`–`:54`). Which thread receives that index is whoever reaches the atomic first. `clock_gettime` plus an atomic increment inside a range callback is uneven. It moves the next `fetch_add` to a different thread.

A clock read on the main thread before `ParallelFor` only delays the publish. Workers are still waiting on the condition variable (`:69`). The steal race starts at `notify_all` (`:153`).

The one-second dump belongs on the main thread, after the barrier, through `ConsolePrintf` (`WoW/Source/Console/ConsoleClient.cpp:941`). A `ConsolePrintf` inside a range callback takes the console lock on a worker and stretches that worker’s chunk.

Counter atomics stay off the `Pool` object (`JobSystem.cpp:27`–`:28`). Sharing a cache line with `g_pool.next` makes the steal itself bounce.

### Hook sites on a steal

These are the campaign sites that run inside `ParallelFor`. An enabled counter here changes which thread owns the next chunk. Each callback writes only its own index. The main thread consumes the array after `ParallelFor` returns.

| Site | Job | Why the bytes stay put |
| --- | --- | --- |
| `FillDoodadSkin` `CM2SceneRender.cpp:598`, launched at `:795` when `instances >= 8` | Skinning | Vertex span is `vertices + n * stride` (`:603`). `TransformVertices_*` uses stack matrices (`M2Model.cpp:3101`). |
| `FillOwnedRange` `MapRenderChunk.cpp:198`, launched at `:333` | Terrain fill | Overlapping locks skip the job and fill serially (`:317`). `CreateVertices` (`MapChunk.cpp:968`) does not write the clip buffer. |
| `FillMappedRange` `DetailDoodad.cpp:610`, launched at `:732` | Grass fill | Locks happen on the main thread in `QueueBatchFill` (`:690`) before the job. `FillVertices` (`:340`) uses stack `matrices[4]` and the adds already seeded in `CreateDetailDoodads`. |
| `CullDoodadRange` `World.cpp:3907`, launched at `:3966` when `size >= 32` | Exterior doodad cull | Writes `culls[i].inFrustum` / `occluded` only. `AddDoodadDefModelToModelScene` runs after the barrier in vector order (`:3971`). |
| `RunChunkVisibility` `MapChunk.cpp:452`, launched at `:578` | Visibility flush | Writes `VisJob` and `LiquidOut` slots. `LinkChunkBucket` (`:427`) runs after the barrier in request order (`:580`). |

`FlushQueuedVisibility` (`MapChunk.cpp:514`), `CullDoodadsExterior` (`World.cpp:3924`), `CancelOwnedFill` (`MapRenderChunk.cpp:206`), and `DropQueuedVisibility` (`MapChunk.cpp:390`) are main-thread wrappers. A counter beside the `ParallelFor`, or beside the serial erase, does not enter the steal. The erase still removes the matching chunk and leaves the rest of the vector in place.

`DrawRigidDoodads` (`CM2SceneRender.cpp:608`), `AllocInstances` (`M2Shared.cpp:771`), `QueueMultiPass` / `FlushMultiPass` (`MapRenderChunk.cpp:2526`, `:2559`), `IBuildUniforms` (`CGxDeviceVulkan.cpp:971`), `Draw` (`:1358`), `IBufLock` (`:1589`), `AcquireBuffers` (`DetailDoodad.cpp:117`), `UseStreamingBufs` / `RenderSetup` (`MapRenderChunk.cpp:889`, `:910`), `CreateIndices` (`MapChunk.cpp:302`), and both `vkAllocateMemory` sites are main-thread or render-thread. Call order fixes draw order, uniform bytes, and the stream cursor. Wall time there changes when the next `ParallelFor` starts. It does not reshuffle a steal that has not been published.

`RenderSetup` calls `FlushOwnedFills` first (`MapRenderChunk.cpp:911`), and that function is the terrain `ParallelFor`. A counter inside `FillOwnedRange` is the steal. A counter in `RenderSetup` before that call is the pre-publish delay.

`ClipBufferUpdate` (`MapChunk.cpp:1302`) writes `s_clipBuffer` through the shared `s_clipBufferVerts[9]` scratch (`CWorldOcclusion.cpp:43`, `:336`). It runs serially from `UpdateOcclusion` (`World.cpp:2592`–`:2628`), which walks `m_holedChunkList`. It is not a `ParallelFor`. An enabled counter there changes how long each chunk’s silhouette takes. The height update is a per-column max (`CWorldOcclusion.cpp:407`), and the walk order stays the list order. Do not move this function onto the job pool, and do not call it from `CullDoodadRange` or `RunChunkVisibility`. Those jobs only read `s_clipBuffer`.

### Neighboring frame jobs

Campaign 0 does not name these. A zone wrapped around the whole frame, or a counter dropped into the callback, hits the same steal.

| Site | Launch | What stays stable |
| --- | --- | --- |
| `AnimateModelRange` `M2Scene.cpp:247` | `:1728` | Roots are models with no attach parent (`:1718`). Children run on that root’s thread in `AnimateAttachmentsMT` (`M2Model.cpp:4863`). `m_scene->m_time` is an input. A counter that stores the clock into scene time would move bones. |
| `PrepareNameplateRange` `WorldFrame.cpp:1557` | `:1590` | Writes `prep.show` and `prep.position` for units that `MarkSerialNameplates` left free (`:1532`). Plate lists update on the main thread after the barrier (`:1593`). |

`AnimateTextureTransformsMT` keeps a read-only `static C3Vector center` (`M2Model.cpp:4465`). `AnimateParticleST` (`:5820`) and the ribbon scratch arrays (`:7235`) run on the main thread in `AnimateST`, after the animate barrier (`M2Scene.cpp:1731`).

## What the writer can land

A compile-out flag, default off, so the default build does not emit the clock call or the atomic. Increments only, at the sites above. One `ConsolePrintf` per second on the main thread after the jobs have joined. Leave `VkDrawUniforms`, `GpuDrawItem`, the `GxTerrainDraw` prefix, the grass seed, the hole mask, the 220-yard cap, the 160-chunk pool radius, and the cull predicates where they are.
