# Faith — watch list

**Do not land this cycle.**

Campaign 0 has not ranked these above campaigns 1–6. Three proposed changes fail Faith. A rejected approach stops. It does not get a quieter variant in the same cycle. The two passes stay on the watch list until a later campaign, after the log ranks them.

| Proposed change | Result |
| --- | --- |
| Portal cover test | **Pass** (hold) |
| Nameplates hashed by model pointer | **Pass** (hold) |
| `TSFixedArray::ReallocData` trivial `realloc` | **Fail** |
| `Job::ParallelFor` barrier | **Fail** |
| Occlusion silhouette cache | **Fail** |

## Portal cover — Pass

`CMapObj::RenderThruPortals` keeps a pending view when its rect misses every covered rect, then calls `PushPortalView`.

```1320:1330:WoW/Source/WorldClient/MapObj.cpp
        for (j = 0; j < s_coveredRects.Count(); j++) {
            CRect& covered = s_coveredRects[j];

            if (view.m_rect.maxX >= covered.minX && view.m_rect.maxY >= covered.minY && view.m_rect.minX <= covered.maxX && view.m_rect.minY <= covered.maxY) {
                break;
            }
        }

        if (j >= s_coveredRects.Count()) {
            CWorldScene::PushPortalView(&view);
        }
```

The overlap is inclusive on every edge (`>=` / `<=`) at `WoW/Source/WorldClient/MapObj.cpp:1323`. A full-screen covered rect is inserted on the exterior root when the portal faces away (`WoW/Source/WorldClient/MapObj.cpp:1237-1244`). That set is the interior cull.

A later search structure passes when the set of views that reach `PushPortalView` is the same set, including views that only touch an edge. This cycle does not land it. Ironforge is interior-only, and Campaign 0 has not ranked the loop.

## Nameplates — Pass

`MarkSerialNameplates` sets `defer` in two steps. A model that must stay on the calling thread is marked first (`WoW/Source/Ui/WorldFrame.cpp:1535-1538`). The second loop then marks every remaining pair that shares `m_worldModel` (`WoW/Source/Ui/WorldFrame.cpp:1541-1553`). A unit already marked `defer`, and a null model, do not pull another unit into the pair (`WoW/Source/Ui/WorldFrame.cpp:1544-1549`).

Deferred units take `PostAnimate` on this thread. The others take `ShouldShowNamePlate` inside `ParallelFor`, then `NamePlateAddOrUpdate` or `DestroyNamePlate` (`WoW/Source/Ui/WorldFrame.cpp:1590-1605`).

A hash by model pointer passes when the `defer` set matches those two loops. Already-serial models stay out of the hash. This cycle does not land it. The cost is UI scale, and Campaign 0 has not ranked it.

## `TSFixedArray::ReallocData` — Fail

This is verified Storm behavior.

```746:772:Engine/Source/Storm/H/stpl.h
// VERIFY: e2efd710 manual
// OFFSET: [0x00403760<char*>, ... ]
void TSFixedArray<T>::ReallocData(uint32_t count) {
    // ...
    this->m_data = static_cast<T*>(SMemReAlloc(olddata, sizeof(T) * count, this->MemFileName(), this->MemLineNo(), SMEM_FLAG_NOREALLOC));

    if (!this->m_data) {
        this->m_data = static_cast<T*>(SMemAlloc(sizeof(T) * count, this->MemFileName(), this->MemLineNo(), 0));
        // copy-construct, then destroy the old elements
    }
}
```

`SMEM_FLAG_NOREALLOC` is `0x10` (`Engine/Source/Storm/H/storm.h:58`). `SMemReAlloc` returns null when that flag is set and the old pointer is live (`Engine/Source/Storm/Source/SMem.cpp:85-87`), so every grow takes the alloc, copy-construct, destroy path at `Engine/Source/Storm/H/stpl.h:759-770`.

`TSGrowableArray` is a `TSFixedArray`. `SetCount`, `Add`, `GrowToFit`, and `Reserve` all call `ReallocData` (`Engine/Source/Storm/H/stpl.h:794`, `836`, `864`, `915`, `939`, `960`, `985`, `1001`). The `OFFSET:` list on `ReallocData` is every container instantiation in the client. A trivial-type `realloc`, with or without a type-trait gate, changes that path for every array that hits the gate: constructors do not run, the pointer may stay put, and the failure path in `SMemReAlloc` (`Engine/Source/Storm/Source/SMem.cpp:92-96`) is a different function from the null fallback.

`ReallocAndClearData` is a different verified function and passes flags `0` (`Engine/Source/Storm/H/stpl.h:741`). It is not a license to change `ReallocData`.

## `Job::ParallelFor` barrier — Fail

The caller waits until every range is done and every worker has returned to the pool lock.

```158:162:Engine/Source/Base/Job/JobSystem.cpp
    std::unique_lock<std::mutex> lock(g_pool.mu);
    // -- ʕ •ᴥ•ʔ✿ Stay until ranges finish and workers drop fn/next, so the next publish cannot race them. ✿ ʕ •ᴥ•ʔ
    g_pool.cv.wait(lock, [] {
        return g_pool.remaining.load() == 0 && g_pool.finished == g_pool.workerCount;
    });
```

`finished == workerCount` is the hand-off. After it, the main thread uses Gx on the bytes the workers just wrote:

- Terrain fills lock, then `ParallelFor`, then `BufUnlock` (`WoW/Source/WorldClient/MapRenderChunk.cpp:291-344`).
- Grass fills lock, then `ParallelFor`, then `BufUnlock` (`WoW/Source/WorldClient/DetailDoodad.cpp:691-746`).
- Rigid doodad skin locks a stream buffer, then `ParallelFor`, then `BufUnlock` and `Draw` (`Engine/Source/Model2/CM2SceneRender.cpp:776-819`).

A barrier that returns on `remaining == 0` alone lets `BufUnlock` and `Draw` run while a worker is still in `RunRanges`. The next `ParallelFor` then publishes a new `fn` and resets `next` (`Engine/Source/Base/Job/JobSystem.cpp:144-153`) against a worker that has not dropped the old job. That races Gx on both the D3D9 device and the Vulkan device.

The same wait is what makes the cull stores visible. Doodad flags are applied only after `ParallelFor` returns (`WoW/Source/WorldClient/World.cpp:3964-3978`). Chunk and liquid links are applied only after `FlushQueuedVisibility`'s `ParallelFor` (`WoW/Source/WorldClient/MapChunk.cpp:577-592`). Bone results are consumed only after `CM2Scene::Animate`'s `ParallelFor` (`Engine/Source/Model2/M2Scene.cpp:1728-1742`).

## Occlusion silhouette cache — Fail

Each frame clears the horizon to `-1000000` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:97-98`) and rebuilds `s_clipBufferMatrix` from the current view (`WoW/Source/WorldClient/World.cpp:2918-2922`).

`CMapChunk::ClipBufferUpdate` (`VERIFY: c9630247`, `OFFSET: 0x007CFB10`) picks two 9-vertex edges from the camera quadrant, then rasters them.

```1302:1327:WoW/Source/WorldClient/MapChunk.cpp
void CMapChunk::ClipBufferUpdate() {
    uint32_t indices[9];
    uint32_t base = 0;

    if (CWorldScene::s_activeWorldView.x > CWorldScene::camTarget.x) {
        base = 136;
    }
    // ...
    CWorldOcclusion::UpdateBufferChunk(..., this->m_header->m_holes);
    // second edge from the Y quadrant, base 0 or 8
}
```

`UpdateBufferChunk` projects those verts through `s_clipBufferMatrix` and raises `s_clipBuffer` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:338-342`, `406-409`). A hole mask punches columns to `-1000001` (`WoW/Source/WorldClient/CWorldOcclusion.cpp:351-373`). Hole-free chunks update only inside the near/far depth window; holed chunks always update (`WoW/Source/WorldClient/World.cpp:2612-2628`).

`QueryBuffer` returns `0` (visible) when the object sits above the stored horizon, and `2` when the span is covered (`WoW/Source/WorldClient/CWorldOcclusion.cpp:518-524`, `579-585`). Doodads treat `>= 2` as occluded (`WoW/Source/WorldClient/World.cpp:3916-3917`).

A cache on height and quadrant skips that raster. The column stays at the frame clear of `-1000000`, or it keeps a horizon from another matrix in the same quadrant. `QueryBuffer` then returns `0`. The doodad, liquid, or chunk stays in the scene. That is a pop-in. Height and quadrant do not name the view matrix, the hole punch, or the depth gate, so the cached outline is a different cull predicate.

## Close

Fail `ReallocData`, the job barrier, and the occlusion cache. Leave the portal cover test and the nameplate hash on the watch list. Do not land any of the five this cycle.
