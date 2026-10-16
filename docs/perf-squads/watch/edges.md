# Edges — watch list

Campaign 0 has not ranked these above campaigns 1–6. The notes below are the cases that make a later trivial-type `realloc`, a shorter `ParallelFor` wait, or a silhouette cache disagree with the current results.

## `TSFixedArray::ReallocData`

`ReallocData` always passes `SMEM_FLAG_NOREALLOC` (`Engine/Source/Storm/H/stpl.h:757`, flag `Engine/Source/Storm/H/storm.h:58`). When `olddata` is live, `SMemReAlloc` returns null and does not free (`Engine/Source/Storm/Source/SMem.cpp:85-87`). The caller then allocates, copy-constructs, destroys the old elements, and `SMemFree`s the old block (`Engine/Source/Storm/H/stpl.h:759-770`). A null `olddata` still allocates, because `SMemReAlloc` handles a null pointer before the flag (`Engine/Source/Storm/Source/SMem.cpp:79-80`).

`TSGrowableArray` is a `TSFixedArray`. `SetCount`, `Add`, `GrowToFit`, `New`, and `Reserve` all reach this function (`Engine/Source/Storm/H/stpl.h:794`, `:836`, `:864`, `:915`, `:939`, `:960`, `:985`, `:1001`). The gate is per `T`. `TSGrowableArray<CParticle2>` (`Engine/Source/Services/ParticleSystem2.h:153`) and `TSGrowableArray<CParticle2_Model>` (`:154`) share `ReallocData` (`Engine/Source/Services/ParticleSystem2.cpp:2231-2234`).

### Copy or destructor side effects

`CParticle2` is the float record. `CParticle2_Model` adds a user copy constructor and a user destructor.

```2669:2698:Engine/Source/Services/ParticleSystem2.cpp
CParticle2_Model::CParticle2_Model(const CParticle2_Model& source) {
    // ...
    if (this->m_model) {
        this->m_model->m_refCount++;
    }
}

CParticle2_Model::~CParticle2_Model() {
    if (this->m_model) {
        this->m_model->Release();
        this->m_model = nullptr;
    }
}
```

The current grow runs both sides: placement `T(olddata[i])` then `olddata[i].~T()` (`Engine/Source/Storm/H/stpl.h:766-767`). The `AddRef` and the `Release` pair. A bitwise `realloc` runs neither side on the prefix that moves. That pair stays balanced only when the kept prefix is moved as raw bytes and the discarded tail still runs `~T()` first (`Engine/Source/Storm/H/stpl.h:752-754`), while the old block is still the live allocation.

These splits change the refcount:

| What runs | Result |
| --- | --- |
| Tail `[count, m_count)` skips `~T()` | Each dropped model skips `Release`. The ref stays elevated. |
| Tail `~T()` runs after `realloc` has shortened the block | The destructor runs on storage `realloc` already gave up. `Release` reads a dead `m_model`. |
| Copy constructor runs and the old prefix skips `~T()` | `m_refCount++` at `ParticleSystem2.cpp:2680` has no matching `Release`. |
| Old prefix runs `~T()` after a bitwise move of the same bytes | `Release` drops the ref the surviving element still holds. The later destructor `Release`s again. |

`CXMLAttribute` is the same function through `TSGrowableArray<CXMLAttribute>` (`Engine/Source/FrameXML/XMLTree.h:40`). Each attribute holds two `RCString`s (`:22-23`). `RCString` has a virtual destructor (`Engine/Source/Base/RCString.h:13`), so the element is not a trivial type. A gate that sees "two pointers" and calls `realloc` skips that destructor at the old address.

`std::is_trivially_copyable<T>` is the check that rejects both instantiations. It requires a trivial destructor as well as a trivial copy. `is_trivially_copy_constructible` alone, `is_standard_layout`, or a size cutoff still admits a user destructor. `CParticle2` may take the byte path. `CParticle2_Model` stays on the copy at `stpl.h:766-767`.

### Shrink to zero

`SetCount(0)` does not call `ReallocData`. It calls `Clear` (`Engine/Source/Storm/H/stpl.h:801-803`), which runs `~TSFixedArray` and then stores `m_count = 0` and `m_data = nullptr` (`:722-727`). The destructor frees with `SMemFree` (`:714-715`).

`ReallocData(0)` with a live block destroys every element (`:752-754`), sets `m_alloc = 0` (`:756`), and, under `SMEM_FLAG_NOREALLOC`, gets a null from `SMemReAlloc` while `olddata` is still allocated (`SMem.cpp:85-87`). The fallback `SMemFree(olddata)` (`stpl.h:770`) is then the only free. `m_count` is unchanged inside `ReallocData`. `SetCount` writes it only on the non-zero arm (`stpl.h:800`).

A trivial branch that calls `SMemReAlloc` with flags `0` and `sizeof(T) * 0` hits a different null. The rounded size is 0 (`SMem.cpp:89`). glibc `realloc(ptr, 0)` frees `ptr` and returns null. `SMemReAlloc` skips `FatalError` because `size` is 0 and returns that null (`:92-97`). `ReallocData` treats every null as "old block still live" and `SMemFree`s `olddata` (`stpl.h:762-770`). That is a second free of the block `realloc` already released. `copycount` is 0, so the copy loop does not run. The free still does.

`ReallocAndClearData` already passes flags `0` (`stpl.h:741`), after every destructor (`:733-735`). It stores the pointer `SMemReAlloc` returns and does not free `olddata` again. That shape is what makes size 0 survivable there. `ReallocData`'s null arm always frees.

Routing `Clear` through `realloc(ptr, 0)` also leaves `m_count` at the old value when the caller expected `Clear` to zero it (`:725`). `~TSFixedArray` then destroys `m_count` elements at the pointer `realloc` returned (`:710-712`). For `CParticle2_Model` those destructors already ran in the prefix loop, so `Release` runs twice.

### `realloc` failure

For a non-zero size, a failed `realloc` leaves the original block allocated and returns null. `SMemReAlloc` then calls `FatalError` (`SMem.cpp:92-94`). `FatalError` calls `StormExitProcess` only while `s_exitonfailure` is set (`:34-36`). The default is on (`:9`). `SMemSetDebugFlags` clears it through `SMEM_DEBUG_NOEXITONFAILURE` (`:17-18`). After that return, `SMemReAlloc` still returns null (`:97`) and the original block is still live.

`ReallocData` saves `olddata` before the call (`stpl.h:750`) and has already stored `m_alloc = count` (`:756`). The fallback allocates a new block, copy-constructs from `olddata`, destroys the old elements, and frees `olddata` (`:759-770`). That recovers the failure only while `olddata` still owns the bytes.

A trivial path that assigns `m_data = SMemReAlloc(...)` and returns on null drops the only pointer to the live block. `m_alloc` already says the new count. The array destructor frees a null `m_data` and the old block stays allocated. Sharing the existing `if (!m_data)` arm distinguishes nothing: size 0 and out-of-memory are the same null (`SMem.cpp:92-97`). Size 0 has already freed. Out-of-memory has not. `SMEM_FLAG_NOREALLOC` is what makes a null from a live pointer always mean "old block still valid."

A successful `realloc` that moves the block must not enter the copy loop. `olddata[i]` is then not a live object. An in-place success that also placement-news `T(olddata[i])` starts a second lifetime in the same bytes and, for `CParticle2_Model`, pairs an extra `AddRef` with `~T()` on that same object (`stpl.h:766-767`).

## `Job::ParallelFor`

One pool, one published job (`Engine/Source/Base/Job/JobSystem.cpp:23-28`). `ParallelFor` is main-thread only. `fn` must not call `Job::` or anything on `g_theGxDevicePtr` (`Engine/Source/Base/Job/JobSystem.h:19-20`). The caller and the workers take ranges with the same `next` fetch (`JobSystem.cpp:47`). The caller does not own index 0.

### Nested wait

`WorkerMain` waits for `epoch`, drops the lock, runs `RunRanges`, then takes the lock and does `finished += 1` (`JobSystem.cpp:69-85`). `ParallelFor` publishes `fn`, resets `finished` to 0, bumps `epoch` (`:144-153`), runs `RunRanges` on the caller (`:156`), then waits until `remaining == 0` and `finished == workerCount` (`:160-162`).

A range function that calls `ParallelFor` with `count > 0` publishes a second job onto that same slot. `count == 0` returns before the publish (`:133-135`).

From a worker, the nested call happens inside `RunRanges` (`:82`), so the worker does not hold `mu`. The nested wait needs `finished == workerCount`. The worker that is inside that wait does not reach `finished += 1` (`:84`) until the wait returns. The other workers stop at `workerCount - 1`. `cv.wait` releases `mu` while it sleeps (`:158`), so those other workers can increment. They cannot make up the missing one. The outer wait on the client thread uses the same predicate, so it stays asleep with them.

From the client thread, the nested call happens inside the outer `RunRanges` (`:156`), before the outer wait. Workers still inside the outer `fn` then `remaining.fetch_sub` (`:56`) against the inner counter, and their `finished += 1` satisfies the inner wait. Workers blocked in `cv.wait` (`:69`) wake on the new `epoch` and run the inner `fn`. The inner wait can complete, because the client thread is not one of the `workerCount` finishers. The outer wait then observes `finished == workerCount` and `remaining == 0` from the inner job. Outer ranges that have not run are already past the barrier, and a worker still inside the outer `fn` is about to touch `next` after the next publish (`:149`).

### Gx from a worker

`fn` runs on whichever thread wins `next.fetch_add` (`JobSystem.cpp:47`). A device call in `fn` runs on a `wow-job` thread.

`CGxDeviceVulkan::IBufLock` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1589`) calls `VkContext::StreamLock` (`:1605`), which bumps the frame stream cursor in `BumpChunk` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1044`) with no lock. The stream path `new`s and `delete`s a `VkBufferAlloc` (`CGxDeviceVulkan.cpp:1614-1634`). `Draw` records into `frame.cmd` (`:1374-1375`): `vkCmdBindDescriptorSets` at `:1442`, `vkCmdDrawIndexed` at `:1470`. That command buffer has one recorder.

The range functions that exist today write a pointer the client thread locked before `ParallelFor`, and the client thread unlocks after the wait:

| Fill | Lock | `ParallelFor` | Unlock |
| --- | --- | --- | --- |
| Doodad skin | `Engine/Source/Model2/CM2SceneRender.cpp:777` | `:795` | `:812` |
| Terrain | `WoW/Source/WorldClient/MapRenderChunk.cpp:291`, `:301` | `:333` | `:337`, `:342` |
| Grass | `WoW/Source/WorldClient/DetailDoodad.cpp:691`, `:699` | `:732` | `:739`, `:744` |

`FillDoodadSkin` (`CM2SceneRender.cpp:598`) writes through `s_transformVertices`. `FillOwnedRange` (`MapRenderChunk.cpp:198`) and `FillMappedRange` (`DetailDoodad.cpp:610`) write the mapped vertices and indices. `CM2Scene::Animate` reads `g_theGxDevicePtr` at `Engine/Source/Model2/M2Scene.cpp:1710` before `ParallelFor` at `:1728`. `UseVertexColorOrder` reads `Caps()` at `DetailDoodad.cpp:595` before the grass `ParallelFor`. `AnimateMT` and `FillVertices` do not call the device.

`BufLock`, `BufUnlock`, `Draw`, `RsSet`, and `ShaderConstantsLock` from a range function race `BumpChunk`'s cursor and `frame.cmd` with the client thread. The client thread is inside the same `ParallelFor`, in its own `RunRanges`, so the race includes the caller's slice of `fn`. The header names `g_theGxDevicePtr` (`JobSystem.h:20`). The same rule covers the D3D9 device behind that pointer.

## Occlusion silhouettes

`CMapChunk::ClipBufferUpdate` (`WoW/Source/WorldClient/MapChunk.cpp:1302`) builds two 9-vertex edges and passes them to `CWorldOcclusion::UpdateBufferChunk` (`:1314`, `:1327`). Each call reads `heights[*indices]` plus `offset->z`, then multiplies by `s_clipBufferMatrix` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:338-342`). The solid path writes the min projected Y into `s_clipBuffer` (`:388`, `:406-409`). `Render` clears that buffer to `-1000000` first (`WoW/Source/WorldClient/CWorldSceneRender.cpp:97-98`). `QueryBuffer` returns `0` when the object is above the stored column and `2` when the span covers it (`CWorldOcclusion.cpp:580-581`).

`vertexList` is one static 145-vector (`MapChunk.cpp:61`). `ClipBufferUpdate` uses it for XY. Z comes from `m_height`. `Intersect` stores `m_height` into `vertexList[i].z` (`:1690-1692`) on that shared array. A cache taken from `vertexList.z` aliases every chunk onto the last intersect.

### Height edit

`m_height` is the MCVT float payload (`MapChunk.cpp:208-209`). The pointer is assigned there and cleared in the constructor (`:1160`). `UpdateBufferChunk` samples nine of those floats per edge (`CWorldOcclusion.cpp:341`).

A cache keyed on the chunk address or on the `m_height` pointer keeps the previous horizon after a store into those floats. The pointer does not change. The projected Z does: `heights[*indices] + offset->z` (`:341`), and `offset` is `m_topLeftCoords` (`MapChunk.cpp:1314`). A write to one of the nine samples, or a write of a new MCVT image into the same buffer, changes the ridge `QueryBuffer` compares against (`CWorldOcclusion.cpp:580`). The world-space samples have to be the nine floats (and `m_topLeftCoords.z`), taken again after the store.

`m_header->m_holes` selects a different write. A non-zero mask punches columns to `-1000001` and returns (`CWorldOcclusion.cpp:351-373`). A height key that ignores the mask replays the solid ridge across a hole. Hole-free chunks call `ClipBufferUpdate` only inside the near/far depth window. Holed chunks always call it (`WoW/Source/WorldClient/World.cpp:2612-2628`). A cached outline that skips the call leaves the frame clear of `-1000000` in those columns.

The projected span also depends on `s_clipBufferMatrix`. A cache of the nine world positions can survive a camera move that keeps the same heights. A cache of the values written into `s_clipBuffer` cannot. Those values are recomputed from the matrix on every call (`CWorldOcclusion.cpp:342`).

### Camera quadrant change

The two edges are chosen from the camera against the look target. Both tests are strict `>`:

| Edge | Test | Indices |
| --- | --- | --- |
| Row | `s_activeWorldView.x > camTarget.x` (`MapChunk.cpp:1306-1307`) | `136..144` when true, `0..8` when false (`:1310-1312`) |
| Column | `s_activeWorldView.y > camTarget.y` (`:1318-1319`) | base `8` step `17` when true, base `0` step `17` when false (`:1322-1325`) |

Equality stays on base 0. The two bits are independent, so there are four index pairs. The pair is a property of the camera, so it changes for every chunk in the frame when the view point crosses the target on X or on Y. One crossing stales one edge. Both crossings stale both.

`farCornerIndex` uses the opposite comparison, `camTarget.x > s_activeWorldView.x` (`WoW/Source/WorldClient/World.cpp:2897-2903`). `nearCornerIndex` uses `camTarget.x < s_activeWorldView.x` (`:2907-2913`), which matches the clip test, including equality mapping to 0. `cornerVertexIndex` is `{0, 8, 136, 144}` (`MapChunk.cpp:64`): four corners, not the nine-vertex edge. A cache keyed on `farCornerIndex` stores the opposite row and column whenever the camera is off the axis.

`s_clipBufferMatrix` is rebuilt in the same `CWorldScene::Update` (`World.cpp:2918-2924`). A dolly that does not flip either `>` test still changes that matrix. World-space samples of the nine heights stay valid across that dolly. The horizon written at `CWorldOcclusion.cpp:406-409` does not. Skipping `ClipBufferUpdate` on a stable quadrant leaves the clear value from `CWorldSceneRender.cpp:97-98`, and `QueryBuffer` returns `0`.
