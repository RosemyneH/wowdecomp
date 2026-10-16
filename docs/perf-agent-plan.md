# Performance squad plan

Static cost ranking of the processes that move the most bytes and do the most repeated work. There is no hierarchical profiler in tree (`MemUsage`, `HeapUsage2`, and the Lua `TogglePerformance*` APIs are stubs). Campaign 0 builds the measurements. Later campaigns do not start until that log exists.

Game design stays put: on-disk ADT / WDT / WDL / M2, placement seeds, frustum and occlusion rules, alpha order, and the 768-byte `VkDrawUniforms` / 448-byte `GpuDrawItem` shader records. `// OFFSET:` functions stay behavior-compatible. The work is process and layout: who allocates, how often, and how fat the per-frame records are.

The working tree already has uncommitted renderer work (`CGxDeviceVulkan`, `VkContext`, `CM2SceneRender`, `MapRenderChunk`, `World`, `Ffp.vert`). Squads branch from that work. They do not revert it.

## How a campaign runs

Each optimization gets the same ten agents. They stay on that campaign until it closes. One writer at a time. Everyone else reads the diff and the latest log line.

| # | Agent | Keeps doing |
| --- | --- | --- |
| 1 | Counter | Zones with `OsGetAsyncTimeClocks`. Counters only. No behavior change. |
| 2 | Accountant | Turns the log into bytes, allocs, and draws per frame. |
| 3 | Layout | Names the next structure: fields, owner, lifetime, capacity. |
| 4 | Writer | Lands one slice. One concern. No drive-by edits. |
| 5 | Faith | Checks seeds, cull predicates, draw order, `// OFFSET:` behavior, shader pins. |
| 6 | Edges | Holes, dual chunks, shaders on and off, D3D fallback, pool exhaustion, indoors. |
| 7 | Leak | Retire paths, in-flight GPU buffers, capacity that shrinks every frame. |
| 8 | Integrator | Applies the review fixes. Keeps the branch buildable. |
| 9 | Scene | Holds the capture scenes steady so before and after match. |
| 10 | Loop | Remeasures. Opens the next slice or closes the campaign. |

Cycle: 1 → 2 → 3 → 4 → 5, 6, 7 in parallel → 8 → 9 → 10. Agent 10 starts the next cycle or writes `CLOSED` with the numbers.

Close when the campaign budget below is met, or two cycles show no gain, or Faith rejects the approach. A rejected approach stops. It does not get a quieter variant in the same cycle.

Scenes, same character position and camera each run:

- Outdoor: terrain, doodads, grass (Elwynn / Westfall density).
- City: many skinned and rigid M2s.
- Interior: portal-heavy WMO (Ironforge).
- Crowd: many nameplates.

Log one line per second: zone times, alloc counts, stream bytes, `vkAllocateMemory` count, uniform uploads, descriptor binds, terrain batch size, draw count.

## Order

Campaign 0 runs alone. After its baseline log:

| Parallel group | Campaigns | Why they can share a machine |
| --- | --- | --- |
| A | 1 | Owns `CM2SceneRender`, `M2Shared`, skinning entry points. |
| B | 2, then 6 | Both touch `CGxDeviceVulkan` / `VkContext`. 6 starts when 2 closes. |
| C | 5 | Owns visibility in `MapChunk` and doodad cull scratch in `World`. |
| D | 3, then 4 | Both touch `MapRenderChunk`. 4 starts when 3 closes. |

Watch-list items get a squad only if Campaign 0 shows them in the top of the log.

## Campaign 0 — Frame log

**Why first.** Every later budget is a guess until this exists. `World::GetFramerate` is a 30-sample tick average. `HeapUsage` only sees `ObjectAlloc`. The hot `std::vector` and Vulkan traffic is invisible.

**Build.** A compile-out flag, default off, that increments atomics in the sites Campaigns 1–6 name. Dump through the existing console, not a new subsystem. Timers are `OsGetAsyncTimeClocks` (`CLOCK_MONOTONIC` on Linux).

**Budget.** A one-second log line from each of the four scenes, with non-zero samples for stream locks, uniform uploads, terrain batch size, and visibility flushes.

**Slices.** Declare the counters. Wire outdoor + M2 + terrain + grass sites. Wire Vulkan alloc / uniform / descriptor sites. Print the line. Confirm a scene run. Freeze the format so later squads do not rename fields.

## Campaign 1 — M2 mesh traffic

**Process.** Vulkan is on the fixed-function path (`CShaderEffect::s_enableShaders == 0`), so bones are applied on the CPU.

`DrawRigidDoodads` (`CM2SceneRender.cpp`) already proves a section is one bone, then still stream-locks and rewrites every vertex and index of that section on every batch before `DrawInstanced`. The mesh does not change. The per-instance data is `m_boneMatrices[bone]`.

The multi-bone fallback (`FillDoodadSkin`, `CM2Model::SetVertices` → `TransformVertices_*`) skins into a stream buffer that the next lock throws away.

`CM2Shared::AllocInstances` rounds capacity up and `DestroyBuffers` on the way, so a dense area rebuilds the shared pools at the peak.

**Layout.**

- Rigid sections bind the shared static vertex and index buffers already built for that skin section. Upload matrices only.
- Skinned sections write once per `(model, section, frame)` into a small dynamic pool and reuse that buffer for every batch that shares it.
- Instance capacity doubles in place. Pools are not destroyed on every step up.

**Must stay.** Sort order, material setup, env-map UV paths, multi-sample sections, and the shader-on branch that already uses shared vertices.

**Budget.** Rigid batches move matrix bytes only (no mesh rewrite). Skinned sections skin once per model per frame. `DestroyBuffers` does not run on the steady camera.

**Slices.** Static bind for the one-bone path. Frame cache for CPU skin. Hysteresis on `AllocInstances`. Golden check on a city camera.

## Campaign 2 — Per-draw uniform pack

**Process.** `CGxDeviceVulkan::Draw` calls `IBuildUniforms` every time: `memset` of 768 bytes, then matrix, light, fog, and texture-stage packing. A `memcmp` of those 768 bytes decides whether to memcpy into the 32 MiB uniform ring. `vkCmdBindDescriptorSets` runs on every draw after that, including when the set and offset did not change.

**Layout.** Dirty bits from `Xform*` / `RsSet` / light updates. Skip the pack when clean. Bind the descriptor set when the set or dynamic offset changes.

**Must stay.** `VkDrawUniforms` stays 768 bytes (`static_assert`, `Tools/spirv-pin-compare.py`). Missed dirty state is a wrong frame, so the bit set is the whole campaign.

**Budget.** Uniform uploads and descriptor binds scale with state changes, not with draw count, on the outdoor scene.

**Slices.** Dirty-bit pack. Conditional descriptor bind. Pin-compare still matches.

## Campaign 3 — Terrain multipass records

**Process.** `QueueMultiPass` copies a `TerrainCapture` per chunk: chunk pointer, three `C44Matrix`s, and four light blocks, about 468 bytes. `CaptureLights` rereads the same device lights per chunk. `FlushMultiPass` allocates `used` and `draws` every flush, then scans layers × chunks × chunks to pair textures, then copies the light blocks again into `GxTerrainDraw` (the 448-byte GPU record plus draw args).

**Layout.**

- One light block per flush. Per chunk: pointer, world matrix, tex matrices, index range, base vertex.
- `s_terrainBatch`, `used`, and `draws` keep their capacity (`clear` + `reserve`).
- Pair layers with a sort or a small hash on `(texture, layer texture)` instead of the nested scan. Alpha order stays layer 0, then later layers.

**Must stay.** Which chunks enter the batch (`special` flags, pool identity). Shadow pass after the batches. `GxTerrainDraw` GPU layout.

**Budget.** Capture bytes per chunk drop to matrices plus the draw args. Flush scratch does not allocate after warmup. Batch count matches the baseline log.

**Slices.** Slim capture + shared lights. Persistent scratch. Texture grouping. Outdoor screenshot against the Campaign 0 camera.

## Campaign 4 — Stop remeshing the terrain and grass

**Process.** Two miss paths rebuild the same mesh every frame:

- `AcquireBuffers` in `DetailDoodad.cpp`: if the grass pool is short, vertex and index pointers stay null. The comment on that path says the shared stream buffer is wiped by the next lock, so the fallback refills every frame. `FillVertices` rebuilds instance matrices when `hasData` is clear.
- `CMapRenderChunk::RenderSetup`: a null `m_chunkBuf` calls `UseStreamingBufs`, which locks and refills. `AllocBuf` returns null when the free list is empty.
- `CMapChunk::CreateIndices` writes the same 8×8 pattern (768 `uint16`s) for every hole-free chunk. `UpdatePools` sizes the index pool at `768 * count`, with count derived from a far-clip radius capped at 160.

**Layout.**

- Grass stays on pool slots. A miss skips or fades that batch for the frame. It does not stream.
- Terrain prefers the existing LRU on `s_bufList` over a stream refill. Streaming remains for the debug line path.
- One static index buffer for `m_holes == 0` (and the dual-chunk base of +145). Private indices only when the hole mask is non-zero.

**Must stay.** Hole masks, 145-vertex topology, winding, grass seeds, fade distance, the 220-yard detail-distance behavior.

**Budget.** `UseStreamingBufs` and grass `FillVertices` stay at zero on a steady outdoor camera inside the current pool. Index pool bytes drop by the hole-free share.

**Slices.** Counters for the two miss paths (if Campaign 0 was coarse). Grass misses do not stream. Terrain LRU steal. Shared hole-free indices.

## Campaign 5 — Cull and visibility scratch

**Process.** `FlushQueuedVisibility` swaps `s_visQueue`, then allocates `jobs` and `liquids` sized to that flush. `CullDoodadsExterior` clears and refills `s_doodadCulls` every exterior cull. `CancelOwnedFill` and `DropQueuedVisibility` erase in the middle of a vector.

**Layout.** File-scope or thread-local vectors that `clear()` and keep `capacity`. Cancel by swap-with-last or a generation stamp, same membership after the flush.

**Must stay.** Parallel visibility results, sort-bucket linking, frustum and occlusion thresholds, the barrier around doodads that are not loaded yet.

**Budget.** Those three flushes do not call `malloc` after the first spike on the outdoor scene. Cancel cost is O(1).

**Slices.** Visibility arena. Doodad-cull arena. Stamp or swap-erase for cancels.

## Campaign 6 — Vulkan buffer lifetime

**Process.** Images go through `VkHeap` (64 MiB blocks, `std::map` freelist). Vertex, index, uniform, and staging buffers take a separate `vkAllocateMemory` of host-visible coherent memory. `CreateBuffer` does not place static pools in device-local memory. Oversized stream locks still `new` / `delete` a `VkBufferAlloc` around the pool pointer (`IBufLock`). The stream-ring wrap that used to allocate a fresh 2.75 MiB pool is already fixed (`StreamRingSize` is 64 MiB). Do not reopen it.

**Layout.**

- Host rings suballocate from a few large host-visible allocations.
- `GxPoolUsage_Static` uploads once into device-local memory and retires the staging copy.
- `VkHeap` freelist becomes size-class or an intrusive list. Same split and merge results.
- Borrowed stream views live inside `CGxPool` / `CGxBuf`. No heap `VkBufferAlloc` per lock.

**Must stay.** Frame garbage retirement (`VkGarbage`), the uniform ring overflow guard that refuses to reuse another draw’s matrices, spare-buffer caps already in `VkContext`.

**Budget.** `vkAllocateMemory` per frame is flat while flying across the outdoor scene. Static mesh pools are device-local after upload. Stream locks do not call `operator new`.

**Slices.** Embed the borrowed buffer view. Host suballocator. Device-local static upload. Heap freelist structure.

## Watch list

No squad until Campaign 0 ranks them above the six campaigns.

| Item | Evidence | Why it waits |
| --- | --- | --- |
| Portal cover test | `MapObj.cpp` nested loop of pending views × covered rects | Interior-only. Same predicate if it is promoted. |
| Nameplates | `MarkSerialNameplates` is O(n²) over visible units | UI scale. Hash by model pointer if promoted. |
| `TSFixedArray::ReallocData` | Always passes `SMEM_FLAG_NOREALLOC`, so every grow allocs and copy-constructs | Original Storm behavior, every container in the client. A trivial-type `realloc` is a later, separate campaign with a type-trait gate. |
| `Job::ParallelFor` barrier | Each call waits for every worker | Only if Campaign 1 still spends time inside small doodad jobs. |
| Occlusion silhouettes | `ClipBufferUpdate` rebuilds two 9-vertex outlines per eligible chunk | Cache on height and quadrant if the log shows it. |

## Faith rules for every writer

- Do not change placement math, hole tests, or cull thresholds to make a pool smaller.
- Do not change `VkDrawUniforms` or `GpuDrawItem` field order. `Tools/spirv-pin-compare.py` stays green.
- Do not retune far clip, grass distance, or pool radii except where Campaign 4 replaces a miss with an existing LRU eviction.
- D3D9 and the shader-on path keep their current results.
- Comments in new code use the repo’s TextEmoji form.
