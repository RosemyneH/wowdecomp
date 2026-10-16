# Interior UI edges

Watch list in `docs/perf-agent-plan.md` (portal cover, nameplates). Neither item is promoted until Campaign 0 ranks it. A promotion keeps the predicates below. Interior scene is a portal-heavy WMO (Ironforge). Nameplates are the crowd scene.

`MarkSerialNameplates` is the O(n²) scan over visible units (`WoW/Source/Ui/WorldFrame.cpp:1532`). The cover test is the pending-view × covered-rect loop (`WoW/Source/WorldClient/MapObj.cpp:1316`).

| Edge | Ruling |
| --- | --- |
| Zero visible units | Empty list. No `ParallelFor`, no plate add or destroy in this function. |
| One unit | `ready` is 0 or 1. Caller `PostAnimate` only. |
| Many units on one model | Every non-deferred member of that pointer is `defer = 1`, including the first. |
| Null `m_worldModel` | Stays parallel-eligible. Nulls are not one bucket. `GetNamePosition` still assumes the pointer is live. |
| Portal view, no covered rects | Every pending view is pushed. |
| Overlapping, not containing | Closed AABB overlap rejects the view. Containment is a different test. |

## Nameplates

`PostAnimateVisibleUnits` clears `s_visibleUnits`, enumerates visible objects, marks serial rows, then either runs `ShouldShowNamePlate` on workers or calls `PostAnimate` on the caller (`WoW/Source/Ui/WorldFrame.cpp:1573`).

`CollectVisibleUnit` stores `defer = 0`, `show = 0`, and a zero position (`WoW/Source/Ui/WorldFrame.cpp:1507`). Those zeros are the plate result only when `computed` is true and the row is not deferred (`WoW/Source/Ui/WorldFrame.cpp:1596`).

`computed` is `ready > 1` (`WoW/Source/Ui/WorldFrame.cpp:1584`). `ready` counts rows with `defer == 0` (`WoW/Source/Ui/WorldFrame.cpp:1580`). `ParallelFor` receives `s_visibleUnits.size()`, and `PrepareNameplateRange` skips deferred rows (`WoW/Source/Ui/WorldFrame.cpp:1563`, `WoW/Source/Ui/WorldFrame.cpp:1590`). The job barrier waits for every worker (`Engine/Source/Base/Job/JobSystem.cpp:160`).

`PostAnimate` is `ShouldShowNamePlate`, then `NamePlateAddOrUpdate` or `DestroyNamePlate` (`WoW/Source/Object/ObjectClient/Unit_C.cpp:799`). The parallel apply path calls the same add or destroy using the worker’s `show` and `position` (`WoW/Source/Ui/WorldFrame.cpp:1601`). Plate lists stay on the caller. Workers write `prep` only.

### Zero visible units

`s_visibleUnits.clear()` then an enum that appends nothing leaves `size() == 0` (`WoW/Source/Ui/WorldFrame.cpp:1574`). Both loops in `MarkSerialNameplates` are bounded by that size (`WoW/Source/Ui/WorldFrame.cpp:1535`, `WoW/Source/Ui/WorldFrame.cpp:1541`). `ready` is 0, `computed` is 0, `ParallelFor` is not called. The apply loop does not run, so this function does not call `DestroyNamePlate` or `NamePlateAddOrUpdate`.

`std::vector::clear` keeps capacity. The next non-empty frame refills the same buffer. A promotion that sweeps the live plate list because the visible vector is empty changes frames where a unit dropped out of `ClntObjMgrEnumVisibleObjects` without this function destroying its plate.

`Job::ParallelFor(0)` returns before the barrier (`Engine/Source/Base/Job/JobSystem.cpp:133`). This function does not rely on that. The zero-unit frame never enters `ParallelFor`.

### One unit

The pair loop starts at `j = i + 1` (`WoW/Source/Ui/WorldFrame.cpp:1548`). With `count == 1` it does not run. `defer` comes only from `NameplateModelStaysSerial` (`WoW/Source/Ui/WorldFrame.cpp:1536`).

`ready` is 0 when that model stays serial, and 1 when it does not. Both fail `ready > 1`, so `computed` is 0. The unit takes `PostAnimate` on the caller (`WoW/Source/Ui/WorldFrame.cpp:1596`). `show` is still 0 from collection and is ignored.

A single ready unit that is sent through the apply branch without `PrepareNameplateRange` hits `DestroyNamePlate`, because `show` is still the collected 0. The `ready > 1` gate is what keeps that unit on `PostAnimate`. One unit also skips the job barrier.

### Many units on one model

`m_worldModel` is one `CM2Model*` on the object (`WoW/Source/Object/ObjectClient/Object_C.h:102`). The pair test is pointer equality (`WoW/Source/Ui/WorldFrame.cpp:1549`).

`NameplateModelStaysSerial` is a property of that model: attach parent, attach list, missing scene, or loaded without `m_fFlags & 1` (`WoW/Source/Ui/WorldFrame.cpp:1521`). Every unit that shares the pointer gets the same answer. The first loop sets `defer` on all of them, and the pair loop then skips them because both sides require `!defer` (`WoW/Source/Ui/WorldFrame.cpp:1544`, `WoW/Source/Ui/WorldFrame.cpp:1549`).

When the model does not stay serial, the pair loop is the only writer. On a match it sets `defer` on `i` and on `j` (`WoW/Source/Ui/WorldFrame.cpp:1550`). It does not break, so every later unit with that pointer is marked too. The first unit is marked. A hash that leaves index 0 clear and defers only later hits is a different membership.

That membership changes the thread only when `ready` stays above 1. Example: two units share model `M`, and two other units have distinct non-serial models. Correct result: both `M` rows are deferred, `ready == 2`, the two unique rows run in `PrepareNameplateRange`, both `M` rows take `PostAnimate`. Leaving the first `M` clear makes `ready == 3` and runs `ShouldShowNamePlate` for that unit on a worker.

If those shared-model units are the whole list, every row is deferred, `ready` is 0, `computed` is 0, and each unit takes `PostAnimate`. `ParallelFor` is not called. A hash table for this case is empty of parallel keys.

The hash key is the pointer value on rows that are still `defer == 0` after `NameplateModelStaysSerial`, and only when the pointer is non-null. A row already deferred is not a witness for anybody else (`WoW/Source/Ui/WorldFrame.cpp:1549`).

### Null `m_worldModel`

`NameplateModelStaysSerial(nullptr)` returns 0 (`WoW/Source/Ui/WorldFrame.cpp:1517`). The pair loop `continue`s when `model` is null, before the equality test (`WoW/Source/Ui/WorldFrame.cpp:1544`). Two nulls compare equal and are still not paired. Each null stays `defer == 0` and counts toward `ready`.

Two nulls, or one null plus another non-deferred unit, set `computed` and call `ShouldShowNamePlate` from the worker. A hash bucket whose key is `nullptr` marks those rows serial and moves them to `PostAnimate`. That is a different `ready`.

`ShouldShowNamePlate` reaches `GetNamePlateScreenPos` after the health, flag, visibility, camera, and distance returns (`WoW/Source/Object/ObjectClient/Unit_C.cpp:20392`). `GetNamePosition` calls `this->m_worldModel->IsLoaded` with no null test (`WoW/Source/Object/ObjectClient/Unit_C.cpp:8162`). `World::QueryObjectVisible` returns `m_culled == 0` (`WoW/Source/WorldClient/World.cpp:4320`), so a culled-clear unit with a null model still gets there. This hole is already in the parallel set. A promotion reads the pointer. It does not dereference it, and it does not put nulls in a bucket to avoid the load.

## Portal cover

`RenderThruPortals` at exterior depth 0 owns the two static arrays (`WoW/Source/WorldClient/MapObj.cpp:1194`). `RenderThruPortalsExterior` clears the interior-pass flag and enters at depth 0 (`WoW/Source/WorldClient/MapObj.cpp:1760`, `WoW/Source/WorldClient/MapObj.cpp:1767`). `RenderInterior` sets the flag and also enters at depth 0 (`WoW/Source/WorldClient/MapObj.cpp:1701`, `WoW/Source/WorldClient/MapObj.cpp:1728`).

Covered rects and pending views are appended only when `s_interiorPass` is clear and `depth` is 0 (`WoW/Source/WorldClient/MapObj.cpp:1238`, `WoW/Source/WorldClient/MapObj.cpp:1289`). The interior pass does not fill either array and does not clear them on entry (`WoW/Source/WorldClient/MapObj.cpp:1197`).

Each back-facing portal (`side < 0`) appends one covered rect, the unit square `min = 0`, `max = 1`, then `continue`s (`WoW/Source/WorldClient/MapObj.cpp:1239`). One square per back-facing portal. The pending view’s rect is a different value, built in `AddInteriorPortalView`.

The cover loop runs after the portal loop, and only when `depth == 0` and the pending list is non-empty (`WoW/Source/WorldClient/MapObj.cpp:1312`). A covered rect appended by a later portal still rejects a view queued earlier. A recursive call (`depth > 0`) returns at that same line, so it does not push and does not clear the depth-0 arrays.

On a hit, `PushPortalView` copies the view into `s_portalViews` (`WoW/Source/WorldClient/World.cpp:2938`). `RenderPortalList` draws that list (`WoW/Source/WorldClient/CWorldSceneRender.cpp:207`). A view with vertices becomes a fan; a view with `m_vertCount == 0` becomes a quad from `m_rect` (`WoW/Source/WorldClient/World.cpp:3251`, `WoW/Source/WorldClient/World.cpp:3268`). Rejecting a view drops that draw. Pushing one adds it.

### Portal view with no covered rects

Entry at exterior depth 0 sets both counts to 0 (`WoW/Source/WorldClient/MapObj.cpp:1197`). Covered rects stay empty when no portal takes the `side < 0` branch. Pending views can still be queued: front-facing, clip survives, exterior, depth 0, `s_cullStateValid`, destination flags without `0x10008` and without `0x140` (`WoW/Source/WorldClient/MapObj.cpp:1284`), then `AddInteriorPortalView` appends only when the ext flags lack `0xC` and `clippedCount > 2` (`WoW/Source/WorldClient/MapObj.cpp:881`, `WoW/Source/WorldClient/MapObj.cpp:895`).

The inner loop is `for (j = 0; j < s_coveredRects.Count(); j++)` (`WoW/Source/WorldClient/MapObj.cpp:1320`). With count 0, `j` is 0 and the body does not run. `j >= Count()` is true, so every pending view is pushed (`WoW/Source/WorldClient/MapObj.cpp:1328`). Empty cover means the whole pending list is drawn.

`AddInteriorPortalView` can also refuse the append. If every candidate is refused, pending count is 0 and the function returns at `WoW/Source/WorldClient/MapObj.cpp:1312` before the cover loop and before the tail `SetCount(0)` (`WoW/Source/WorldClient/MapObj.cpp:1333`). The next exterior depth-0 entry clears both arrays again. The interior pass does not. A promotion that drops the entry clear and also skips the tail clear on an empty pending list leaves the previous frame’s squares in `s_coveredRects`.

### Overlapping but not containing

The reject test is closed AABB overlap (`WoW/Source/WorldClient/MapObj.cpp:1323`):

- `view.maxX >= covered.minX`
- `view.maxY >= covered.minY`
- `view.minX <= covered.maxX`
- `view.minY <= covered.maxY`

Any one covered rect that passes all four stops the inner loop. The view is not pushed. Missing every rect pushes it.

Containment of the view by the covered rect is `view.min >= covered.min` and `view.max <= covered.max` on both axes. The loop does not test that. A partial overlap rejects the view.

The rect written into `s_coveredRects` is only the unit square. A view can still stick out of that square. `TransformAndClipVerts` floors `w` at `0.0001` and then divides (`WoW/Source/WorldClient/MapObj.cpp:658`). `CalcScreenRectFromVerts` stores those x/y values with no NDC clamp (`WoW/Source/WorldClient/World.cpp:4277`). `AddInteriorPortalView` maps with `(v + 1) * 0.5` and keeps out-of-range results (`WoW/Source/WorldClient/MapObj.cpp:900`).

View `minX = -0.25`, `maxX = 0.25`, `minY = 0.25`, `maxY = 0.75` against the unit square: the overlap slab is `x in [0, 0.25]`, `y in [0.25, 0.75]`. The view is not inside the square (`minX < 0`). The square is not inside the view. All four comparisons pass, so the view is not pushed.

A shared edge is a hit. View `maxX == 0`, `maxY == 0`, with both mins below 0, touches the square’s corner. `>=` and `<=` reject it. `CreateInversePortalList` separates with `maxX <= portal.minX` (`WoW/Source/WorldClient/World.cpp:3128`), so a shared edge stays disjoint there. The cover test is the other predicate. `CRect::ContainsPoint` is open (`Engine/Source/Tempest/CRect.cpp:7`). `operator&` returns an intersection box and can come back inverted when the rects miss (`Engine/Source/Tempest/CRect.cpp:25`). The four comparisons are the test a promotion keeps.

A view that misses the square still pushes. View `maxX = -0.2` fails `view.maxX >= covered.minX` against the unit square. With no other covered rect, that view is pushed.
