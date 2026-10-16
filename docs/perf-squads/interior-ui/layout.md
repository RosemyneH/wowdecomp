# Interior-ui layout

Two side tables. `MarkSerialNameplates` groups plates by `CM2Model*` and marks a run deferred when that pointer appears twice. The portal cover test evaluates the same four comparisons through a run index. The plates that leave with `defer == 1`, and the pending views that reach `PushPortalView`, stay the sets those loops already produce.

## Nameplates — linear runs

`MarkSerialNameplates` (`WoW/Source/Ui/WorldFrame.cpp:1532`) runs once per `PostAnimateVisibleUnits` (`WoW/Source/Ui/WorldFrame.cpp:1576`) on `s_visibleUnits` (`WoW/Source/Ui/WorldFrame.cpp:1497`). `CollectVisibleUnit` stores `defer = 0` (`WoW/Source/Ui/WorldFrame.cpp:1509`).

The first loop stays (`WoW/Source/Ui/WorldFrame.cpp:1535`). `NameplateModelStaysSerial` (`WoW/Source/Ui/WorldFrame.cpp:1516`) sets `defer = 1` when the model has an attach parent, an attach list, or no scene, and when it is loaded with `m_fFlags & 1` clear (`WoW/Source/Ui/WorldFrame.cpp:1521-1527`). A null `m_worldModel` returns 0 (`WoW/Source/Ui/WorldFrame.cpp:1517`).

The pair loop (`WoW/Source/Ui/WorldFrame.cpp:1541`) is the part this table replaces. It visits a plate only when `defer` is still 0 and `m_worldModel` is non-null (`WoW/Source/Ui/WorldFrame.cpp:1544`). The field is `CGObject_C::m_worldModel` (`WoW/Source/Object/ObjectClient/Object_C.h:102`). A later plate matches on pointer equality (`WoW/Source/Ui/WorldFrame.cpp:1549`) and both plates become deferred (`WoW/Source/Ui/WorldFrame.cpp:1550-1551`). One pointer, one plate: `defer` stays 0.

### Accepted defer set

| Plates still at `defer == 0` with a non-null model | Run | `defer` after the mark |
| --- | --- | --- |
| One plate for that `CM2Model*` | count 1 | stays 0 |
| Two or more plates for that `CM2Model*` | count ≥ 2 | every plate in the run becomes 1 |
| Null `m_worldModel` | omitted | stays 0 |
| Already 1 from `NameplateModelStaysSerial` | omitted | stays 1 |

A pointer shared only with plates the serial pass already marked is a count-1 run. The non-deferred plate stays eligible. That matches the pair loop, which skips any plate with `defer` set on both sides (`WoW/Source/Ui/WorldFrame.cpp:1544`, `WoW/Source/Ui/WorldFrame.cpp:1549`).

`s_visibleUnits` order stays the enum order. `PrepareNameplateRange` skips `defer` (`WoW/Source/Ui/WorldFrame.cpp:1563`). The parallel call (`WoW/Source/Ui/WorldFrame.cpp:1590`) and the `PostAnimate` walk (`WoW/Source/Ui/WorldFrame.cpp:1593`) both use that index order. The runs are a side index.

### `NameplateModelRun`

| Field | Type | Role |
| --- | --- | --- |
| `model` | `CM2Model*` | Pointer for this run. Never null. |
| `begin` | `uint32_t` | First slot in `s_nameplateIndex`. |
| `count` | `uint32_t` | Plates in the run. A mark fires at `count >= 2`. |

`s_nameplateIndex` is `uint32_t` slots into the `units` argument. One slot per plate the pair loop would have used as `i` or `j`.

**Owner.** Both vectors are file-scope statics beside `s_visibleUnits` in `WoW/Source/Ui/WorldFrame.cpp`. `MarkSerialNameplates` is the only writer. `PrepareNameplateRange` does not read them.

**Lifetime.** Cleared and refilled on every call. Slot values are valid only until that call returns. They alias the current `units` vector, which is `s_visibleUnits` after `clear` and the visible-object enum (`WoW/Source/Ui/WorldFrame.cpp:1574-1576`).

**Capacity.** `clear()` then refill. No `shrink_to_fit`. High water is the crowd scene’s `s_visibleUnits.size()`. The first spike allocates. A steady crowd reuses that capacity. `s_nameplateIndex` holds at most `n` slots. The run vector holds at most `n` runs.

### Build

1. Run the serial pass unchanged (`WoW/Source/Ui/WorldFrame.cpp:1535`).
2. `clear` both vectors.
3. Push `i` when `units[i].defer` is 0 and `units[i].unit->m_worldModel` is non-null.
4. Order `s_nameplateIndex` by `uintptr_t` of that pointer. Equal addresses form one contiguous run. Pointer `<` across different objects is unspecified; the integer cast is the order. The comparator’s only key is the address, so a tie cannot split a run.
5. One forward scan. A new `NameplateModelRun` starts when the pointer changes. `count >= 2` sets `defer = 1` on every indexed plate. `count == 1` writes nothing.

The scan is one pass over the ordered slots. The sort is the only superlinear step. The pair loop’s `n(n−1)/2` compares (`WoW/Source/Ui/WorldFrame.cpp:1548`) go away. The mark set is the same: every non-deferred, non-null pointer that occurs at least twice.

Worked set, after the serial pass:

| Index | Model | In the index |
| --- | --- | --- |
| 0 | M1, already deferred | no |
| 1 | M2 | yes |
| 2 | null | no |
| 3 | M2 | yes |
| 4 | M3 | yes |
| 5 | M1 | yes |

Runs: M2 ×2, M3 ×1, M1 ×1. Indices 1 and 3 become deferred. Indices 2, 4, and 5 stay 0. Index 0 stays 1.

## Portals — cover runs

`CMapObj::RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1165`) keeps `s_pendingPortalViews` and `s_coveredRects` as function statics (`WoW/Source/WorldClient/MapObj.cpp:1194`). Both reset at the start of an exterior depth-0 walk (`WoW/Source/WorldClient/MapObj.cpp:1197`) and after the cover test (`WoW/Source/WorldClient/MapObj.cpp:1333`). `TSGrowableArray::SetCount(0)` destroys elements and leaves `m_alloc` (`Engine/Source/Storm/H/stpl.h:991`).

Covered rects are appended only for a back-facing portal at exterior depth 0 (`WoW/Source/WorldClient/MapObj.cpp:1238`). Each append is the unit square `minX = 0`, `minY = 0`, `maxX = 1`, `maxY = 1` (`WoW/Source/WorldClient/MapObj.cpp:1239`). Pending views are appended only from `AddInteriorPortalView` at exterior depth 0 when `s_cullStateValid` and `destFlags & 0x140` is clear (`WoW/Source/WorldClient/MapObj.cpp:1289`). `AddInteriorPortalView` itself returns on `m_flags & 0xC` (`WoW/Source/WorldClient/MapObj.cpp:881`) and only stores a view when `clippedCount > 2` (`WoW/Source/WorldClient/MapObj.cpp:895`). Those gates stay.

The nested test runs only when `depth == 0` and the pending list is non-empty (`WoW/Source/WorldClient/MapObj.cpp:1312`). It walks pending views in index order (`WoW/Source/WorldClient/MapObj.cpp:1316`). A view is rejected on the first covered rect that overlaps (`WoW/Source/WorldClient/MapObj.cpp:1323`). A view that misses every rect is pushed (`WoW/Source/WorldClient/MapObj.cpp:1328`). `PushPortalView` appends (`WoW/Source/WorldClient/World.cpp:2938`), so pending order is the push order.

### Overlap predicate

A covered rect rejects a view when all four comparisons are true (`WoW/Source/WorldClient/MapObj.cpp:1323`):

```
view.m_rect.maxX >= covered.minX
view.m_rect.maxY >= covered.minY
view.m_rect.minX <= covered.maxX
view.m_rect.minY <= covered.maxY
```

`CRect` stores `minY`, `minX`, `maxY`, `maxX` (`Engine/Source/Tempest/CRect.h:11`). The predicate uses those fields. Touching edges overlap (`>=` and `<=`). A view is pushed when every covered rect fails at least one comparison.

The unit square does not reject a view that sits fully outside it. A view with `maxX < 0` fails `maxX >= 0`. A view with `minX > 1` fails `minX <= 1`. Those views are pushed today, and they stay pushed. The index stores the rects that were appended. It does not turn “any cover exists” into a reject-all.

`CreateInversePortalList` separates with `maxX <= minX` and the three sibling tests (`WoW/Source/WorldClient/World.cpp:3128`). That test treats a shared edge as disjoint. The cover index uses the four comparisons above. `CRect::operator&` (`Engine/Source/Tempest/CRect.cpp:25`) builds an intersection rectangle. A later area check treats a shared edge as empty. The cover index does not call it.

### `PortalCoverRun`

| Field | Type | Role |
| --- | --- | --- |
| `minX`, `minY`, `maxX`, `maxY` | `float` | One distinct covered rect. The four comparisons read these. |
| `prefixMaxX` | `float` | `std::fmax` of `maxX` over this run and every earlier run in `minX` order. |

`s_coverOrder` is `uint32_t` indices into `s_coveredRects`, used while the runs are built.

**Owner.** Function-static `TSGrowableArray<PortalCoverRun> s_coverRuns` and `TSGrowableArray<uint32_t> s_coverOrder`, declared next to the two existing arrays in `RenderThruPortals` (`WoW/Source/WorldClient/MapObj.cpp:1194`). The same thread recurses (`WoW/Source/WorldClient/MapObj.cpp:1308`). Children return at `WoW/Source/WorldClient/MapObj.cpp:1312` before the index is built. The parent builds it after the portal loop, in the block that starts at `WoW/Source/WorldClient/MapObj.cpp:1316`.

**Lifetime.** Rebuilt for that depth-0 query, then `SetCount(0)` in the same places the two arrays clear (`WoW/Source/WorldClient/MapObj.cpp:1197`, `WoW/Source/WorldClient/MapObj.cpp:1333`). An early return with an empty pending list (`WoW/Source/WorldClient/MapObj.cpp:1312`) leaves the index empty. Runs hold copied floats, not pointers into `s_coveredRects`.

**Capacity.** `SetCount(0)` keeps `m_alloc`. High water is the interior scene’s covered-rect count and pending-view count. `s_coverOrder` holds one index per appended rect. `s_coverRuns` holds one run per distinct 16-byte `CRect`. Today every append is the same unit square (`WoW/Source/WorldClient/MapObj.cpp:1239`), so a full cover list collapses to one run. A miss that used to scan every copy (`WoW/Source/WorldClient/MapObj.cpp:1320`) tests that run once. A hit already stopped on the first copy (`WoW/Source/WorldClient/MapObj.cpp:1324`).

### Build

After the pending list is final, and only when the test at `WoW/Source/WorldClient/MapObj.cpp:1312` would enter the loop:

1. `SetCount(0)` on the run array. Fill `s_coverOrder` with `0 .. s_coveredRects.Count()-1`.
2. Order those indices by `minX` ascending. Break remaining ties with `memcmp` of the 16-byte `CRect` so bitwise-equal rects are adjacent. `+0` and `-0` may land in different runs. Both are tested. The four comparisons treat them as equal (`-0.0 >= 0.0` is true).
3. One forward scan. Adjacent rects with `memcmp == 0` become one run. Copy `minX`, `minY`, `maxX`, `maxY` from the first rect in the run.
4. Set `prefixMaxX` with `std::fmax` along the run array.

Every `Add` at `WoW/Source/WorldClient/MapObj.cpp:1244` still happens. The multiset of covered rects is unchanged. The run array is the query form of that multiset: one record per distinct rect.

### Query

Walk `s_pendingPortalViews` from index 0 through `Count()-1`. For view `V`:

1. No runs: push. Same as `j >= s_coveredRects.Count()` when the cover list is empty (`WoW/Source/WorldClient/MapObj.cpp:1328`).
2. Suffix cut: first run whose `minX > V.m_rect.maxX`. That comparison is `!(V.m_rect.maxX >= covered.minX)`. Runs at and after the cut fail the first conjunct. They are not overlaps. The cut uses `>`, so `minX == V.m_rect.maxX` stays in the prefix. A cut at index 0 leaves an empty prefix, and the view is pushed.
3. Prefix cut: read `prefixMaxX` on the last run still inside the prefix. When `prefixMaxX < V.m_rect.minX` is true, every `maxX` in that prefix fails `V.m_rect.minX <= covered.maxX`. The view is pushed. The cut uses `<`, so `prefixMaxX == V.m_rect.minX` stays for the four comparisons. `std::fmax` drops a NaN `maxX`. A NaN coordinate fails its comparison, so that rect cannot reject a view.
4. For each remaining run, evaluate the four comparisons. The first true result rejects the view. If none are true, push.

`PushPortalView` is called for the same views, in the same pending order.

| View against the unit-square run | Result |
| --- | --- |
| `min` 0.2, `max` 0.4 on both axes | four comparisons true, not pushed |
| `minX` 1.2, `maxX` 1.4, `Y` inside | `prefixMaxX` 1 < 1.2, pushed |
| `maxX` −0.1 | `minX` 0 > −0.1, pushed |
| `maxX` 0, `minX` −0.2, `Y` inside | edge stays in the prefix, four comparisons true, not pushed |
| `X` inside, `minY` > 1 | `X` conjuncts hold, `Y` fails, pushed |

Three copies of the unit square are one run. One inside view and one outside view each take one predicate evaluation. The inside view is rejected. The outside view is pushed.
