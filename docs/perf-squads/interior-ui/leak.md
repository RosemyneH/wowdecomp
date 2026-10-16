# Interior UI leak

Leak review of the interior-ui watch list (`docs/perf-agent-plan.md:167-168`). `s_visibleUnits` is cleared every animate and already keeps its buffer. No grouping map exists in tree. A map added for `MarkSerialNameplates` stays in scope only when it allocates nothing after warmup and drops every model pointer before the mark pass returns.

## s_visibleUnits

`CGWorldFrame::OnWorldRender` calls `PostAnimateVisibleUnits` once per world render (`WoW/Source/Ui/WorldFrame.cpp:1844`).

The function begins with `s_visibleUnits.clear()` (`WoW/Source/Ui/WorldFrame.cpp:1574`). The vector is file-scope (`WoW/Source/Ui/WorldFrame.cpp:1497`). `clear()` leaves `capacity()` in place. `CollectVisibleUnit` refills it with `push_back` (`WoW/Source/Ui/WorldFrame.cpp:1512`). Another allocation happens only when the visible-unit count exceeds that capacity. After the crowd high-water mark, a steady or smaller set does not allocate, and the buffer stays.

`NameplatePrep` stores `CGUnit_C* unit` (`WoW/Source/Ui/WorldFrame.cpp:1486`). It stores no `CM2Model*`. `clear()` destroys the elements. The unit pointer and `C3Vector` are not owners. Units and models stay with the object manager.

The same storage is then used in place: `batch.units = s_visibleUnits.data()` (`WoW/Source/Ui/WorldFrame.cpp:1589`) for `Job::ParallelFor`. Nothing pushes onto `s_visibleUnits` after `CollectVisibleUnit` returns (`WoW/Source/Ui/WorldFrame.cpp:1575`), so that pointer stays valid through the parallel section and the follow-up loop (`WoW/Source/Ui/WorldFrame.cpp:1593-1605`).

`shrink_to_fit` after `clear()` would drop the buffer every animate and allocate it again on the next fill. Leave the capacity.

## Grouping map

`MarkSerialNameplates` (`WoW/Source/Ui/WorldFrame.cpp:1532-1554`) groups with a nested compare of `unit->m_worldModel` (`WoW/Source/Ui/WorldFrame.cpp:1542`, `WoW/Source/Ui/WorldFrame.cpp:1549`). The watch list promotes that to a hash by model pointer (`docs/perf-agent-plan.md:168`). This function has no map today.

### After warmup the map allocates nothing

The map is file-scope scratch, same retire pattern as `s_visibleUnits`. Each animate reuses the storage from the previous high-water mark of distinct `m_worldModel` values.

A node container fails this gate. `std::map` and `std::unordered_map` allocate an element node on insert and free it on `clear()`. The next animate allocates those nodes again. A bucket array that survives `clear()` still costs one node allocation per key. `TSHashTable::InternalNew` / `InternalDelete` do the same with `SMemAlloc` / `SMemFree` (`Engine/Source/Storm/H/stpl.h:1025-1027`). A function-local map allocates on every animate, including the first steady frame.

Storage that passes: a `std::vector` of groups, or an open-address table inside a vector, whose `clear()` keeps capacity. A node pool whose free list absorbs the next frame's inserts also passes. `operator new` and `SMemAlloc` for the grouping structure stay at zero once the distinct-model count has stopped climbing. No `shrink_to_fit`.

### The map holds no dangling model pointer

Keys are the `CM2Model*` values read from `m_worldModel` during `MarkSerialNameplates`. Those pointers are valid while the unit still owns them.

`CGObject_C::SetModel` stores the pointer and increments `m_refCount` (`WoW/Source/Object/ObjectClient/Object_C.cpp:1938-1943`). `CGObject_C::ReleaseModel` calls `Release()` and then writes `nullptr` (`WoW/Source/Object/ObjectClient/Object_C.cpp:1049-1058`). `CM2Model::Release` deletes the object on the last ref (`Engine/Source/Model2/M2Model.cpp:585-588`) through `CM2ModelHeap::Delete`, which runs the destructor and `ObjectFree` (`Engine/Source/Model2/M2Scene.cpp:1518-1520`). `CObjectHeap::Delete` pushes the slot back onto `m_indexStack` (`WoW/Source/ObjectAlloc/ObjectAlloc.cpp:38-41`). The next `CObjectHeap::New` pops that index and returns the same address (`WoW/Source/ObjectAlloc/ObjectAlloc.cpp:202-209`). `CM2ModelHeap::New` placement-news a new model there (`Engine/Source/Model2/M2Scene.cpp:226-237`).

A `CM2Model*` kept past that `Delete` is a dangling key. The recycled slot can compare equal to a different live model.

The map's size is 0 before `MarkSerialNameplates` returns. `PostAnimateVisibleUnits` then runs `ShouldShowNamePlate` / `PostAnimate` (`WoW/Source/Ui/WorldFrame.cpp:1567-1604`) and returns to `OnWorldRender`. Despawn between renders calls `ReleaseModel`. A static map that waits until the next animate to `clear()` still holds the previous keys across that despawn.

Values are indices or counts. They take no ref. The hit-test list is a separate owner: `UpdateObject` does `m_refCount++` (`WoW/Source/Ui/WorldFrame.cpp:1754`) and `MoveToFreeList` calls `Release()` (`WoW/Source/Ui/WorldFrame.cpp:966`). The grouping map takes no extra ref. An extra ref that misses `Release` keeps the `ObjectAlloc` slot alive. An extra ref dropped while the key remains is the dangling case above.

`CGUnit_C::GetObjectModel` returns `m_mountModel` when a mount is set (`WoW/Source/Object/ObjectClient/Unit_C.cpp:5648-5653`). The loop in tree keys `m_worldModel` only. This review binds the pointer that is stored, for the span it is stored.

## Portal cover scratch

`RenderThruPortals` keeps `s_pendingPortalViews` and `s_coveredRects` as function-static `TSGrowableArray`s (`WoW/Source/WorldClient/MapObj.cpp:1194-1195`). Both get `SetCount(0)` at the exterior root (`WoW/Source/WorldClient/MapObj.cpp:1198-1199`) and again after the cover test (`WoW/Source/WorldClient/MapObj.cpp:1333-1334`). `TSGrowableArray::SetCount` on a shrink runs destructors and writes `m_count` (`Engine/Source/Storm/H/stpl.h:1007-1013`). `m_alloc` stays. After the interior high-water mark these two arrays do not allocate.

`s_coveredRects` stores `CRect` values (`WoW/Source/WorldClient/MapObj.cpp:1239-1244`). No model pointers.

`s_portalVerts` follows the same retire path: `SetCount(0)` in `CMapObj::PrepareUpdate` (`WoW/Source/WorldClient/MapObj.cpp:1620`), initial `Reserve(1024)` (`WoW/Source/WorldClient/MapObj.cpp:2182`). `CPortalView::m_verts` (`WoW/Source/WorldClient/CPortalView.h:14`) aliases that buffer (`WoW/Source/WorldClient/MapObj.cpp:898-901`). The alias is taken, then `SetCount` may grow. A grow calls `TSFixedArray::ReallocData` (`Engine/Source/Storm/H/stpl.h:749-757`) with `SMEM_FLAG_NOREALLOC`. When the block cannot expand in place, the old block is freed after the copy (`Engine/Source/Storm/H/stpl.h:759-770`). Views that already hold `m_verts` then dangle. Once `s_portalVerts.m_alloc` covers the frame's vert count, `SetCount` does not reallocate, and those pointers stay inside the same block for the rest of the fill.

`CWorldScene::Update` drops `s_exteriorPortalViews` and `s_portalViews` with `SetCount(0)` (`WoW/Source/WorldClient/World.cpp:2829-2830`) before the later `CMap::PrepareUpdate` that zeros `s_portalVerts` (`WoW/Source/WorldClient/World.cpp:1548`, `WoW/Source/WorldClient/World.cpp:1567`, `WoW/Source/WorldClient/MapObj.cpp:1620`). A cover structure that keeps `CPortalView`s across that `PrepareUpdate` holds pointers into a buffer that is about to be overwritten or moved.

## Gate

- `s_visibleUnits` keeps `clear()` and the existing capacity. The animate path does not shrink it.
- The grouping map allocates only while the distinct-model count is still rising. Later inserts reuse that storage.
- Every `CM2Model*` in that map is gone before `MarkSerialNameplates` returns. The map takes no extra `m_refCount`.
- Portal view and cover arrays keep `m_alloc` across `SetCount(0)`. `m_verts` stays inside the current `s_portalVerts` block.
