# Campaign 0 — Accountant

Static bytes and mallocs from the current tree. The one-second line is a sum (`docs/perf-squads/log/loop.md`). Per-frame figures below are one frame. Ratios of two fields on that line cancel the frame count. Field names stay as frozen: `terrain_batch`, `uniform_uploads`, `draws`, `stream_bytes`, `stream_locks`, `allocs`, `vis_flushes`, `vk_alloc`.

LP64. Pointer **8**. `C44Matrix` is 16 floats, **64** bytes, alignment 4 (`Engine/Source/Tempest/C44Matrix.h:21-36`).

## TerrainCapture

`WoW/Source/WorldClient/MapRenderChunk.cpp:2443-2459`

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `chunk` | 0 | 8 |
| `world` | 8 | 64 |
| `tex0` | 72 | 64 |
| `tex1` | 136 | 64 |
| `lightPos` | 200 | 64 |
| `lightDiffuse` | 264 | 64 |
| `lightAmbient` | 328 | 64 |
| `lightAtten` | 392 | 64 |
| `fogStart` | 456 | 4 |
| `fogEnd` | 460 | 4 |
| `fogColor` | 464 | 4 |
| `fog` | 468 | 4 |
| `indexCount` | 472 | 4 |
| `firstIndex` | 476 | 4 |
| `baseVertex` | 480 | 4 |
| tail pad | 484 | 4 |
| **sizeof** | | **488** |

`8 + 3×64 + 4×64 + 4×4 + 3×4 = 8 + 192 + 256 + 16 + 12 = 484`, then 4 bytes of tail pad because the struct alignment is 8. The plan’s “about 468” is `8 + 192 + 256 + 12`: pointer, three matrices, four light blocks, draw args. The struct also stores the fog key (`fogStart` through `fog`, 16 bytes) and the pad.

`QueueMultiPass` value-inits one `TerrainCapture` and `push_back` copies it (`MapRenderChunk.cpp:2542`, `MapRenderChunk.cpp:2556`). Chunks rejected at `MapRenderChunk.cpp:2537-2539` take no copy. `s_terrainBatch.clear()` keeps capacity (`MapRenderChunk.cpp:2655`), so a warm flush copies **488** bytes per chunk that enters and does not malloc for that copy. `allocs` does not count this `push_back`.

`terrain_batch` is the **max** of `s_terrainBatch.size()` over flushes that pass the empty return (`MapRenderChunk.cpp:2560`). It is not the sum of the 16 `FlushMultiPass` calls (`World.cpp:3672-3728`, 4 passes × 4 layer buckets). Each chunk sits in one bucket, so it is copied once.

| Quantity | Arithmetic | Site |
| --- | --- | --- |
| Largest flush, capture bytes | `terrain_batch × 488` | `MapRenderChunk.cpp:2556` |
| Frame capture bytes | `(sum of non-empty flush sizes) × 488` | same `push_back` |
| `used` malloc | 1 per non-empty flush, `N` bytes | `MapRenderChunk.cpp:2572` `std::vector<uint8_t> used(N)` |
| `GxTerrainDraw` copy | `460` per chunk per layer | `MapRenderChunk.cpp:2601-2603`. sizeof is `3×64 + 4×64 + 12 = 460` (`CGxDevice.h:72-83`) |

The `draws` vector (`MapRenderChunk.cpp:2573`) grows by `push_back` and is destroyed at the end of the flush, so those mallocs happen every non-empty flush. `allocs` does not count them (`loop.md`: the default-constructed `draws` is excluded, and growth is not in the list).

`CaptureLights` memsets the four light blocks, **256** bytes, from the same device lights for every chunk (`MapRenderChunk.cpp:2464-2467`). Those 256 sit inside the 488.

**`terrain_batch`.** On `outdoor`, `terrain_batch ≥ 1` confirms chunks enter the batch, and the largest flush moved `terrain_batch × 488` capture bytes. `terrain_batch == 0` on `outdoor` kills the per-chunk copy for that scene (the batch path at `MapRenderChunk.cpp:2537` did not take). The field cannot confirm the frame sum: a second bucket of the same size is invisible. `allocs` rises by 1 per non-empty flush for `used`. It stays flat across those flushes only if `used` stopped allocating.

## VkDrawUniforms

`static_assert` **768** (`CGxDeviceVulkan.h:46`). A draw that reaches the pack (`CGxDeviceVulkan.cpp:1358`) calls `IBuildUniforms` (`CGxDeviceVulkan.cpp:1359`).

| Step | When | Bytes | Site |
| --- | --- | ---: | --- |
| `memset` of the stack record | every pack | 768 | `CGxDeviceVulkan.cpp:972` |
| `memcmp` against `m_uniforms` | every pack | 768 | `CGxDeviceVulkan.cpp:1377` |
| `memcpy` into the ring | dirty, including the first pack of the frame | 768 | `CGxDeviceVulkan.cpp:1385` |
| cache assign | same dirty path | 768 | `CGxDeviceVulkan.cpp:1388` |

`ISceneBegin` clears `m_uniformsValid` (`CGxDeviceVulkan.cpp:535`), so the first pack of a frame always memcpys. A later pack with the same 768 bytes still memsets and memcmps.

The cursor rounds to `minUniformBufferOffsetAlignment`, then advances by 768 (`CGxDeviceVulkan.cpp:1378`, `CGxDeviceVulkan.cpp:1386`). 768 = 3 × 256, so a 256-byte device alignment consumes **768** ring bytes and no pad. `DrawInstanced` and `DrawTerrainBatch` each call `Draw` once (`CGxDeviceVulkan.cpp:1499`, `CGxDeviceVulkan.cpp:1538`). Instance count does not multiply the 768.

`uniform_uploads` counts the memcpy (`CGxDeviceVulkan.cpp:1385`), not the memset. `draws` counts the GPU command (`CGxDeviceVulkan.cpp:1467`, `:1470`, `:1474`).

| Per frame | Arithmetic |
| --- | --- |
| Ring bytes | `uniform_uploads × 768` |
| CPU clear + compare | packs that reach `CGxDeviceVulkan.cpp:1358` × **1,536** |
| Heap allocs on this path | **0** |

**`uniform_uploads` against `draws`.** `uniform_uploads / draws` near 1 confirms a 768-byte ring copy on essentially every draw (`768 × draws` ring bytes that second). `uniform_uploads` far below `draws` on `outdoor` kills the “uploaded every draw” figure and leaves the memset: the stack rebuild is still 768 per pack, and `uniform_uploads` does not see it. `uniform_uploads == 0` with `draws ≥ 1` kills the ring-copy estimate for that second.

`descriptor_binds` counts `vkCmdBindDescriptorSets` at `CGxDeviceVulkan.cpp:1442`, including an unchanged set and offset. `descriptor_binds / draws` near 1 confirms a bind per draw. That ratio is the Campaign 2 signal, not a byte width.

## DrawRigidDoodads

A call that returns 1 (`CM2SceneRender.cpp:692`) moves:

| Term | Formula | Site |
| --- | --- | --- |
| Vertex stream | `32 × V` | `BufStream(GxPoolTarget_Vertex, 32, V)` at `CM2SceneRender.cpp:644`. The lock writes 8 floats and steps 8 (`CM2SceneRender.cpp:651-660`). |
| Index stream | `2 × I` | `BufStream(GxPoolTarget_Index, 2, I)` at `CM2SceneRender.cpp:667`. Each index is `uint16_t` (`CM2SceneRender.cpp:677-679`). |
| Matrix vector | `64 × N` | `worlds.push_back(m_boneMatrices[bone])` (`CM2SceneRender.cpp:636-642`). `C44Matrix` is 64. |

`V` = `section->m_vertexCount` (`M2Data.h:304`). `I` = `section->m_indexCount` (`M2Data.h:306`). `N` = `total`.

**Bytes moved per successful call = `32×V + 2×I + 64×N`.**

The mesh term is the two stream locks. `IBufLock` on a stream pool returns at `CGxDeviceVulkan.cpp:1640`, and `stream_bytes` adds `buf->m_size` there. So each successful call contributes `32×V + 2×I` to `stream_bytes` and **2** to `stream_locks`. `DrawInstanced` (`CM2SceneRender.cpp:691`) then copies the same `64×N` into `GpuDrawItem.world` (`CGxDeviceVulkan.cpp:1491-1493`). That copy is not `stream_bytes`.

Calls that return 0 before the streams (`CM2SceneRender.cpp:609-634`) add nothing. That includes the shared-vertex flags at `CM2SceneRender.cpp:613`.

`worlds` is function-static. `clear` keeps capacity; `reserve(N)` mallocs only when `N` exceeds it (`CM2SceneRender.cpp:637-638`). A steady camera pays the `64×N` copy and **0** mallocs. Those mallocs are not in `allocs`.

The stream ring is 64 MiB (`VkContext.h:135`). A borrowed lock does not call `vkAllocateMemory`. The first borrowed view on a pool does `new VkBufferAlloc` once (`CGxDeviceVulkan.cpp:1619`); later borrowed locks copy onto that object (`CGxDeviceVulkan.cpp:1623`) and add **0** to `allocs`.

**`stream_bytes` and `stream_locks`.** `stream_bytes` is every successful stream lock, so it is at least the sum of `32×V + 2×I` over calls that returned 1. On `city`, `stream_bytes == 0` with `zone_m2_rigid_ns > 0` kills the rewrite: those calls returned before `CM2SceneRender.cpp:644`. A mean `stream_bytes / stream_locks` of a few dozen bytes is the index lock (`2×I`); thousands of bytes is the vertex lock (`32×V`). The `64×N` matrix term is absent from both fields. `m2_pool_rebuilds` confirms `DestroyBuffers` inside `AllocInstances` (`M2Shared.cpp:779`), which a steady camera leaves at 0.

## Grass stream fallback

`AcquireBuffers` stores null when `s_freeGxBufs.Count() < 2` (`DetailDoodad.cpp:129-131`). `CDetailDoodadInstBatch::Draw` returns at `DetailDoodad.cpp:888-889` before `FillVerticesOnMain` (`DetailDoodad.cpp:893`) and before any `BufLock`. `QueueBatchFill` returns the same way (`DetailDoodad.cpp:670-671`).

**Per missed batch, per frame: 0 stream bytes, 0 `stream_locks`, 0 `allocs`.**

The comment at `DetailDoodad.cpp:129` describes a refill. The statements do not call `BufStream`. A pool hit is `GxPoolUsage_Dynamic` (`DetailDoodad.cpp:569`), so `IBufLock` returns at `CGxDeviceVulkan.cpp:1649` and does not increment `stream_bytes`. `AcquireBuffers` clears `m_hasData` on take (`DetailDoodad.cpp:124-125`). A visible instance sets `m_age = 0` every frame it draws (`World.cpp:3586`) and keeps the buffer, so `FillVertices` runs on acquire, then not again while the instance stays on screen.

The pool is **512** vertex/index pairs (`DetailDoodad.cpp:568-576`, loop `i += 2` over 1024). Default distance is **70** yards (`World.cpp:136`), cap **220** (`World.cpp:1316`). A chunk is `33.333332` yards (`MapChunk.cpp:609`). At 70 yards the disk is about `(70 / 33.333)² × π ≈ 14` chunks, at most 4 batches each (`DetailDoodad.cpp:835`), **56** slots. 512 covers that. At 220 yards the same upper bound is about **548** batches, so some batches miss. Each miss is still the 0-byte return.

A refill that locked the shared stream would be `sizeof(CGxVertexPNCT) × V + 2 × I` every frame, because the next lock wipes that buffer (`CGxDevice.cpp:1073` sets `m_hasData = 0`). `CGxVertexPNCT` is 12 + 12 + 4 + 8 = **36** (`MapObjGroup.h:118-124`). That product is what `stream_bytes` would gain per missed batch per frame. The live miss adds none of it.

**`stream_bytes`.** On a steady `outdoor` camera inside 70 yards, grass adds **0** to `stream_bytes` and **0** to `stream_locks`. The estimate dies when those two keep a per-frame grass-sized term after rigid locks (`32×V + 2×I`) and terrain `UseStreamingBufs` locks are subtracted. A null `m_chunkBuf` streams 145 × 24 = **3,480** vertex bytes (`GxVBF_PN`) or 145 × 28 = **4,060** (`GxVBF_PNC`), plus `768 × 2` = **1,536** index bytes (`MapRenderChunk.cpp:889-900`). A warm terrain pool leaves that at 0 as well (`MapRenderChunk.cpp:930`). `zone_grass_ns > 0` with no leftover in `stream_bytes` confirms the null return.

## FlushQueuedVisibility

`CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`). Empty queue returns at `MapChunk.cpp:515` and does not increment `vis_flushes`. A non-empty queue swaps (`MapChunk.cpp:520`), then constructs:

| Vector | When | Mallocs | Bytes | sizeof |
| --- | --- | ---: | --- | --- |
| `jobs` | every non-empty flush | **1** | `N × 48` | `MapChunk.cpp:524`. `VisJob` is pointer 8 + four 4-byte fields + `C3Vector` 12 + two `uint32_t` = 44, tail pad to **48** (`MapChunk.cpp:359-368`) |
| `liquids` | `liquidTotal > 0` | **1** | `L × 24` | `MapChunk.cpp:549`. `LiquidOut` is pointer 8 + `int32_t` 4 + `C3Vector` 12 = **24** (`MapChunk.cpp:370-374`) |
| `requests.swap` | every non-empty flush | **0** | 0 | `MapChunk.cpp:520` |

`N` is `s_visQueue.size()` at the swap. `L` is the sum of `CChunkLiquid` counts on requests with `wantLiquids` (`MapChunk.cpp:542-546`). `AddMapChunk` enqueues `wantLiquids = 0` (`MapChunk.cpp:600`), then `CullAndBucketChunk` sets that same request’s `wantLiquids` to 1 (`MapChunk.cpp:1123-1124`). A chunk with no liquid list still has `liquidCount == 0`.

Both constructors are in `allocs` (`MapChunk.cpp:524`, and `MapChunk.cpp:549` only when `L > 0`). The swap is not. `EnqueueVisibility` `push_back` (`MapChunk.cpp:424`) mallocs while the queue grows, and the swap hands that buffer to a local that is destroyed at `MapChunk.cpp:594`, so the next frame starts from capacity 0. Those `push_back` mallocs are outside `allocs`.

One allocating flush per rendered frame: `PrepareUpdate` purges first (`Map.cpp:2306`), then `PreUpdateAreas` enqueues, then `CWorldScene::Render` flushes via `UpdatePools` (`CWorldSceneRender.cpp:92`, `MapRenderChunk.cpp:806`). The purge flush sees an empty queue when the previous render already swapped.

**`vis_flushes` and `allocs`.** Each `vis_flushes` increment is one `jobs` malloc of `N × 48`. `allocs / vis_flushes == 1` on a second whose only other `allocs` sites are quiet confirms liquids allocated nothing (`L == 0`) and kills the second malloc. `allocs` rising by **2** per `vis_flushes`, with `used` and `new VkBufferAlloc` quiet, confirms both vectors. The line has no queue length, so it cannot confirm the `48` and `24` widths. Those die only if a later byte counter disagrees with `N × 48` and `L × 24`.

## Per-buffer vkAllocateMemory

`vk_alloc` increments once per `vkAllocateMemory` that returns `VK_SUCCESS`:

| Call | Site | What one success buys |
| --- | --- | --- |
| Buffer | `VkContext::CreateBuffer` `VkContext.cpp:1003` | One host-visible, host-coherent allocation (`VkContext.cpp:1001`). `VkBufferAlloc.size` is the requested size (`VkContext.cpp:984`). |
| Image block | `VkHeap::Alloc` `VkContext.cpp:51` | One device-local block, `64 << 20` or the request when larger (`VkContext.cpp:41`, `VkContext.h:56`). A freelist hit returns at `VkContext.cpp:37` and does not increment. |

`CreateBuffer` callers: the two in-flight rings (`VkContext.cpp:290-300`), `AcquireBuffer` on a spare miss (`VkContext.cpp:979`, from `IPoolBuffer` at `CGxDeviceVulkan.cpp:1546`), and `BumpChunk` for a new chunk (`VkContext.cpp:1034`) or a lock bigger than the chunk (`VkContext.cpp:1024`).

Startup, once, not per frame: `FramesInFlight` is 2 (`VkContext.h:131`) × three buffers.

| Buffer | Size |
| --- | --- |
| Uniform | `32 << 20` = **33,554,432** |
| Items | `8 << 20` = **8,388,608** |
| Indirect | `1 << 20` = **1,048,576** |

**6** `vk_alloc` calls, **86 MiB** host-visible, resident after create.

A stream lock that fits the ring borrows (`CGxDeviceVulkan.cpp:1611-1625`). `PrepareFrame` resets the cursor and keeps `streamChunks` (`VkContext.cpp:722-723`). The first lock that needs a chunk calls `CreateBuffer(64 << 20)` once per in-flight frame (`VkContext.cpp:1034`, `VkContext.h:135`). Later locks in that chunk add **0** to `vk_alloc`. A single lock larger than 64 MiB calls `CreateBuffer` on that lock (`VkContext.cpp:1023-1028`) and `new VkBufferAlloc` every time (`CGxDeviceVulkan.cpp:1634`), which is also one `allocs` increment.

Steady outdoor frame after the rings, the stream chunk, and the terrain and grass pools exist: **`vk_alloc` = 0**. A new `CGxPool` (far-clip rebuild, a newly loaded static mesh) is one more `vk_alloc` of `pool->m_size` on the first `IPoolBuffer`, then 0 while that pool lives.

**`vk_alloc`.** A steady `outdoor` second with `vk_alloc == 0` confirms the per-buffer cost is not per draw and not per stream lock. `vk_alloc` tracking `stream_locks` or `draws` kills that: `CreateBuffer` is running per lock (`VkContext.cpp:1024`) or per draw. `vk_alloc` climbing while `stream_locks` stays flat is the image heap (`VkContext.cpp:51`) or a new pool, still one call per new buffer, not per frame of an existing buffer.
